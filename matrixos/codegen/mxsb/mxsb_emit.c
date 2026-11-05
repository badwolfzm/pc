#define _GNU_SOURCE
/*
 * mxsb_emit -- MXSB v2 super-binary emitter / verifier / dumper.
 *
 *   mxsb_emit build (--elf FILE | --text FILE [--data FILE] [--bss N] --entry OFF)
 *                   --manifest FILE -o OUT.mxsb
 *                   [--superop NAME]... [--build-info k=v]... [--tier baseline|avx2|avx512|multi]
 *                   [--profile NAME] [--vendor ID:FILE]... [--deterministic-only] [--debug-sections]
 *                   (--sign KEY.pem | --unsigned) [--openssl /abs/path]
 *   mxsb_emit verify FILE [--pubkey PUB.pem] [--approve HEX]... [--openssl /abs/path]
 *   mxsb_emit dump FILE
 *   mxsb_emit digest FILE
 *   mxsb_emit upgrade V1FILE --manifest FILE -o OUT.mxsb (--sign KEY.pem | --unsigned)
 *
 * Signing uses the Ed25519 implementation in OpenSSL 3 through an absolute
 * path to the binary (no PATH lookup, no shell), see security review
 * finding 3.  key_id = SHA-256(raw 32-byte public key).
 *
 * The emitter never writes a file that fails its own validator (V1..V25).
 * Exit codes: 0 ok; 1 refused (prints MXSB_E_* and the rule); 2 usage/io.
 */
#include "mxsb.h"
#include "mxsb_elf.h"
#include "mx_sha256.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

static const char *g_openssl = "/usr/bin/openssl";

static int usage(void) {
    fprintf(stderr,
        "usage:\n"
        "  mxsb_emit build (--elf FILE | --text FILE [--data FILE] [--bss N] --entry OFF) --manifest FILE -o OUT\n"
        "                  [--superop NAME]... [--build-info k=v]... [--tier T] [--profile P] [--vendor ID:FILE]...\n"
        "                  [--deterministic-only] [--debug-sections] (--sign KEY.pem | --unsigned) [--openssl PATH]\n"
        "  mxsb_emit verify FILE [--pubkey PUB.pem] [--approve HEXDIGEST]... [--openssl PATH]   (V1..V25, V27, V28 + V20)\n"
        "  mxsb_emit dump FILE\n"
        "  mxsb_emit digest FILE\n"
        "  mxsb_emit upgrade V1FILE --manifest FILE -o OUT (--sign KEY.pem | --unsigned)\n");
    return 2;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "mxsb_emit: cannot open %s: %s\n", path, strerror(errno)); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f); if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    size_t rd = fread(b, 1, (size_t)n, f); fclose(f);
    if (rd != (size_t)n) { free(b); fprintf(stderr, "mxsb_emit: short read on %s\n", path); return NULL; }
    b[n] = 0; *len = (size_t)n; return b;
}
static bool write_file(const char *path, const uint8_t *buf, size_t len) {
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) { fprintf(stderr, "mxsb_emit: cannot write %s: %s\n", tmp, strerror(errno)); return false; }
    bool ok = fwrite(buf, 1, len, f) == len; ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); fprintf(stderr, "mxsb_emit: write failed\n"); return false; }
    if (rename(tmp, path) != 0) { remove(tmp); fprintf(stderr, "mxsb_emit: rename failed: %s\n", strerror(errno)); return false; }
    return true;
}
static int refuse(const mxsb_diag *d) {
    fprintf(stderr, "mxsb_emit: refused: %s (%s): %s\n", mxsb_err_name(d->err), d->rule ? d->rule : "-", d->detail);
    return 1;
}

/* ---- openssl helpers: fork/exec with an argv, no shell -------------------- */
static int run(const char *const argv[], const char *stdin_path, const char *stdout_path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (stdin_path) { int fd = open(stdin_path, O_RDONLY); if (fd < 0) _exit(126); dup2(fd, 0); close(fd); }
        if (stdout_path) { int fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd < 0) _exit(126); dup2(fd, 1); close(fd); }
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    int st = 0; if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static bool tmpname(char *out, size_t n, const char *tag) {
    const char *dir = getenv("TMPDIR"); if (!dir || !*dir || dir[0] != '/') dir = "/tmp";
    snprintf(out, n, "%s/mxsb_%s_XXXXXX", dir, tag);
    int fd = mkstemp(out); if (fd < 0) return false; close(fd); return true;
}
/* raw 32-byte Ed25519 public key from a PEM (private or public) key */
static bool pubkey_raw(const char *pem, bool is_private, uint8_t out[32]) {
    char der[512]; if (!tmpname(der, sizeof der, "der")) return false;
    const char *argv_priv[] = { g_openssl, "pkey", "-in", pem, "-pubout", "-outform", "DER", "-out", der, NULL };
    const char *argv_pub[]  = { g_openssl, "pkey", "-pubin", "-in", pem, "-pubout", "-outform", "DER", "-out", der, NULL };
    int rc = run(is_private ? argv_priv : argv_pub, NULL, NULL);
    size_t n = 0; uint8_t *b = rc == 0 ? read_file(der, &n) : NULL; remove(der);
    if (!b) return false;
    /* SubjectPublicKeyInfo for Ed25519 is 44 bytes: 12-byte prefix + 32-byte key */
    bool ok = n == 44 && memcmp(b, "\x30\x2a\x30\x05\x06\x03\x2b\x65\x70\x03\x21\x00", 12) == 0;
    if (ok) memcpy(out, b + 32 - 20, 32);
    free(b); return ok;
}
static bool sign_digest(const char *keypem, const uint8_t digest[32], uint8_t sig[64]) {
    char din[512], sout[512]; if (!tmpname(din, sizeof din, "dig") || !tmpname(sout, sizeof sout, "sig")) return false;
    FILE *f = fopen(din, "wb"); if (!f) return false; fwrite(digest, 1, 32, f); fclose(f);
    const char *argv[] = { g_openssl, "pkeyutl", "-sign", "-inkey", keypem, "-rawin", "-in", din, "-out", sout, NULL };
    int rc = run(argv, NULL, NULL);
    size_t n = 0; uint8_t *b = rc == 0 ? read_file(sout, &n) : NULL;
    remove(din); remove(sout);
    if (!b) return false;
    bool ok = n == 64; if (ok) memcpy(sig, b, 64); free(b); return ok;
}
static bool verify_sig(const char *pubpem, const uint8_t digest[32], const uint8_t sig[64]) {
    char din[512], sin[512]; if (!tmpname(din, sizeof din, "dig") || !tmpname(sin, sizeof sin, "sig")) return false;
    FILE *f = fopen(din, "wb"); if (!f) return false; fwrite(digest, 1, 32, f); fclose(f);
    f = fopen(sin, "wb"); if (!f) { remove(din); return false; } fwrite(sig, 1, 64, f); fclose(f);
    const char *argv[] = { g_openssl, "pkeyutl", "-verify", "-pubin", "-inkey", pubpem, "-rawin", "-in", din, "-sigfile", sin, NULL };
    int rc = run(argv, NULL, "/dev/null");
    remove(din); remove(sin);
    return rc == 0;
}

/* ---- manifest preparation: replace build_id, normalise ------------------- */
static char *prepare_manifest(const char *text, size_t len, const char *build_id, size_t *outlen, mxsb_diag *d) {
    /* drop any existing build_id line, append ours, then parse+normalise */
    char *tmp = malloc(len + 128); if (!tmp) return NULL;
    size_t w = 0, pos = 0;
    while (pos < len) {
        size_t e = pos; while (e < len && text[e] != '\n') ++e;
        size_t s = pos; while (s < e && (text[s] == ' ' || text[s] == '\t')) ++s;
        bool is_bid = (e - s >= 8 && strncmp(text + s, "build_id", 8) == 0 && (e - s == 8 || text[s+8] == ' ' || text[s+8] == '\t' || text[s+8] == '#'));
        if (!is_bid) { memcpy(tmp + w, text + pos, e - pos); w += e - pos; tmp[w++] = '\n'; }
        pos = e + 1;
    }
    w += (size_t)snprintf(tmp + w, 100, "build_id %s\n", build_id);
    mxsb_manifest m;
    mxsb_err rc = mxsb_manifest_parse(tmp, w, &m, d);
    free(tmp);
    if (rc != MXSB_OK) return NULL;
    char *out = mxsb_manifest_serialize(&m, outlen);
    mxsb_manifest_free(&m);
    return out;
}

typedef struct { const char *elf, *text, *data, *manifest, *out, *sign, *tier, *profile, *vendor[16], *superop[64], *kv[64]; int nvendor, nsuperop, nkv; uint64_t bss, entry; bool have_entry, unsigned_ok, det_only, dbg; } build_args;

static int do_build(build_args *a, bool from_v1, const char *v1path) {
    if (!a->manifest || !a->out) return usage();
    if (!a->sign && !a->unsigned_ok) { fprintf(stderr, "mxsb_emit: choose --sign KEY.pem or --unsigned\n"); return 2; }
    mxsb_diag d = {0};
    uint8_t *text = NULL, *data = NULL; size_t tlen = 0, dlen = 0; uint64_t bss = 0, entry_rel = 0; bool pie = false; uint64_t load_base = 0;
    mxsb_elf_out eo = {0};
    if (from_v1) {
        size_t n; uint8_t *b = read_file(v1path, &n); if (!b) return 2;
        mxsb_image v1; mxsb_err rc = mxsb_decode_v1(b, n, &v1, &d); free(b);
        if (rc != MXSB_OK) return refuse(&d);
        int ti = mxsb_image_find(&v1, MXSB_S_TEXT), di = mxsb_image_find(&v1, MXSB_S_DATA);
        if (ti < 0) { fprintf(stderr, "mxsb_emit: v1 file has no text\n"); mxsb_image_free(&v1); return 1; }
        tlen = (size_t)v1.sec[ti].size; text = malloc(tlen); memcpy(text, v1.sec[ti].data, tlen);
        if (di >= 0) { dlen = (size_t)v1.sec[di].size; data = malloc(dlen); memcpy(data, v1.sec[di].data, dlen); }
        entry_rel = v1.entry_off >= v1.header_size ? v1.entry_off - v1.header_size : 0;
        fprintf(stderr, "mxsb_emit: v1 caps_required=0x%x: the manifest you supply must require the matching families (checked by V24 only if you pass them; the v1 mask is not authoritative)\n", v1.v1_caps_required);
        mxsb_image_free(&v1);
    } else if (a->elf) {
        size_t n; uint8_t *b = read_file(a->elf, &n); if (!b) return 2;
        char err[256];
        if (!mxsb_elf_extract(b, n, &eo, err, sizeof err)) { fprintf(stderr, "mxsb_emit: refused: ELF: %s\n", err); free(b); return 1; }
        free(b);
        text = eo.text; tlen = (size_t)eo.text_size; data = eo.data; dlen = (size_t)eo.data_size; bss = eo.bss_size; entry_rel = eo.entry_rel; pie = eo.pie; load_base = eo.load_base;
    } else {
        if (!a->text || !a->have_entry) return usage();
        text = read_file(a->text, &tlen); if (!text) return 2;
        if (a->data) { data = read_file(a->data, &dlen); if (!data) return 2; }
        bss = a->bss; entry_rel = a->entry;
    }
    if (tlen == 0) { fprintf(stderr, "mxsb_emit: refused: empty TEXT\n"); return 1; }
    if ((dlen || bss) && tlen % MXSB_PAGE != 0) {       /* section 3.4 / V28: zero-pad TEXT to a page before build_id */
        size_t ns = (tlen + MXSB_PAGE - 1) & ~(size_t)(MXSB_PAGE - 1);
        uint8_t *nt = calloc(1, ns); if (!nt) return 2;
        memcpy(nt, text, tlen);
        if (a->elf) { free(eo.text); eo.text = nt; eo.text_size = ns; } else free(text);
        text = nt; tlen = ns;
    }

    char bid[65]; mxsb_build_id(text, tlen, data, dlen, bid);
    size_t mlen; uint8_t *mtext = read_file(a->manifest, &mlen); if (!mtext) return 2;
    size_t nlen; char *norm = prepare_manifest((const char *)mtext, mlen, bid, &nlen, &d); free(mtext);
    if (!norm) return refuse(&d);

    mxsb_image im; mxsb_image_init(&im);
    mxsb_manifest man; if (mxsb_manifest_parse(norm, nlen, &man, &d) != MXSB_OK) return refuse(&d);
    im.caps_summary = man.caps_summary;
    im.flags = (pie ? MXSB_F_PIE : 0) | (a->det_only ? MXSB_F_DETERMINISTIC_ONLY : 0) | (a->dbg ? MXSB_F_DEBUG_SECTIONS : 0);
    mxsb_image_add(&im, MXSB_S_MANIFEST, 0, norm, nlen);
    mxsb_image_add(&im, MXSB_S_TEXT, MXSB_SF_R | MXSB_SF_X, text, tlen);
    if (dlen) mxsb_image_add(&im, MXSB_S_DATA, MXSB_SF_R | MXSB_SF_W, data, dlen);
    if (bss) mxsb_image_add(&im, MXSB_S_BSS, MXSB_SF_R | MXSB_SF_W, NULL, bss);
    if (a->nsuperop) {
        uint32_t ids[64]; int n = 0;
        for (int i = 0; i < a->nsuperop; ++i) {
            int id = mxsb_superop_id(a->superop[i]);
            if (id < 0) { fprintf(stderr, "mxsb_emit: refused: unknown super-op '%s' (names from gpu-commands-ref/rcs.h, provisional)\n", a->superop[i]); return 1; }
            char want[300]; snprintf(want, sizeof want, "superop=%s", a->superop[i]);
            bool req = false; for (int k = 0; k < man.nrequire; ++k) if (strcmp(man.require[k], want) == 0) req = true;
            if (!req) { fprintf(stderr, "mxsb_emit: refused: --superop %s without 'require superop=%s' in the manifest (V25)\n", a->superop[i], a->superop[i]); return 1; }
            bool dup = false; for (int k = 0; k < n; ++k) if (ids[k] == (uint32_t)id) dup = true;
            if (!dup) ids[n++] = (uint32_t)id;
        }
        for (int i = 1; i < n; ++i) { uint32_t t = ids[i]; int j = i; while (j > 0 && ids[j-1] > t) { ids[j] = ids[j-1]; --j; } ids[j] = t; }
        uint8_t raw[64 * 4]; for (int i = 0; i < n; ++i) { raw[4*i] = (uint8_t)ids[i]; raw[4*i+1] = (uint8_t)(ids[i] >> 8); raw[4*i+2] = (uint8_t)(ids[i] >> 16); raw[4*i+3] = (uint8_t)(ids[i] >> 24); }
        mxsb_image_add(&im, MXSB_S_SUPEROPS, 0, raw, (uint64_t)n * 4);
    }
    mxsb_manifest_free(&man);
    {   /* BUILD_INFO: deterministic key order */
        char bi[8192]; int n = 0;
        n += snprintf(bi + n, sizeof bi - (size_t)n, "format=mxsb-2.0\nprofile=%s\nsimd_tier=%s\n", a->profile ? a->profile : "matrixos", a->tier ? a->tier : "baseline");
        n += snprintf(bi + n, sizeof bi - (size_t)n, "toolchain=clang+mxsb_emit\nload_base=0x%llx\npie=%d\n", (unsigned long long)load_base, pie ? 1 : 0);
        n += snprintf(bi + n, sizeof bi - (size_t)n, "layout=text-then-data-page-aligned\n");
        for (int i = 0; i < a->nkv && n < (int)sizeof bi - 300; ++i) n += snprintf(bi + n, sizeof bi - (size_t)n, "%s\n", a->kv[i]);
        mxsb_image_add(&im, MXSB_S_BUILD_INFO, 0, bi, (uint64_t)n);
    }
    for (int i = 0; i < a->nvendor; ++i) {
        const char *colon = strchr(a->vendor[i], ':');
        if (!colon) return usage();
        unsigned long id = strtoul(a->vendor[i], NULL, 0);
        if (id < MXSB_S_VENDOR_MIN || id > 0xFFFFFFFFul) { fprintf(stderr, "mxsb_emit: vendor id must be >= 0x8000\n"); return 2; }
        size_t vl; uint8_t *vb = read_file(colon + 1, &vl); if (!vb) return 2;
        if (vl == 0) { fprintf(stderr, "mxsb_emit: refused: empty vendor section\n"); return 1; }
        mxsb_image_add(&im, (uint32_t)id, 0, vb, vl); free(vb);
    }
    uint8_t zero_sig[96] = {0};
    mxsb_image_add(&im, MXSB_S_SIGNATURE, 0, zero_sig, 96);

    if (mxsb_layout(&im, entry_rel, &d) != MXSB_OK) return refuse(&d);
    uint8_t *buf; size_t blen;
    /* encode validates V1..V25 on the unsigned image (signature bytes are outside the digest) */
    if (mxsb_encode(&im, &buf, &blen, &d) != MXSB_OK) return refuse(&d);
    uint8_t digest[32];
    if (mxsb_digest(buf, blen, digest, &d) != MXSB_OK) return refuse(&d);
    int si = mxsb_image_find(&im, MXSB_S_SIGNATURE);
    uint8_t *sigp = buf + im.sec[si].offset;
    if (a->sign) {
        uint8_t pub[32], sig[64];
        if (!pubkey_raw(a->sign, true, pub)) { fprintf(stderr, "mxsb_emit: cannot read an Ed25519 key from %s via %s\n", a->sign, g_openssl); return 2; }
        if (!sign_digest(a->sign, digest, sig)) { fprintf(stderr, "mxsb_emit: signing failed\n"); return 2; }
        mx_sha256_oneshot(pub, 32, sigp);          /* key_id */
        memcpy(sigp + 32, sig, 64);
        /* self-check: signature must verify with the public key we derived */
        char pubpem[512]; if (!tmpname(pubpem, sizeof pubpem, "pub")) return 2;
        const char *argv[] = { g_openssl, "pkey", "-in", a->sign, "-pubout", "-out", pubpem, NULL };
        bool ok = run(argv, NULL, NULL) == 0 && verify_sig(pubpem, digest, sig);
        remove(pubpem);
        if (!ok) { fprintf(stderr, "mxsb_emit: self-verification of the signature failed\n"); return 2; }
    }
    /* final validation of the exact bytes to be written */
    if (mxsb_validate(buf, blen, &d) != MXSB_OK) return refuse(&d);
    if (!write_file(a->out, buf, blen)) return 2;
    char hx[65]; mx_sha256_hex(digest, hx);
    printf("mxsb_emit: wrote %s (%zu bytes) text=%zu data=%zu bss=%llu entry_rel=%llu caps_summary=0x%llx %s\n  digest=%s\n  build_id=%s\n",
           a->out, blen, tlen, dlen, (unsigned long long)bss, (unsigned long long)entry_rel, (unsigned long long)im.caps_summary,
           a->sign ? "signed" : "UNSIGNED (approvals list only)", hx, bid);
    free(buf); mxsb_image_free(&im); free(norm);
    if (a->elf) mxsb_elf_free(&eo); else { free(text); free(data); }
    return 0;
}

static int do_verify(int argc, char **argv) {
    if (argc < 1) return usage();
    const char *path = argv[0], *pub = NULL, *approve[64]; int napp = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--pubkey") && i + 1 < argc) pub = argv[++i];
        else if (!strcmp(argv[i], "--approve") && i + 1 < argc && napp < 64) approve[napp++] = argv[++i];
        else if (!strcmp(argv[i], "--openssl") && i + 1 < argc) g_openssl = argv[++i];
        else return usage();
    }
    if (g_openssl[0] != '/') { fprintf(stderr, "mxsb_emit: --openssl must be an absolute path\n"); return 2; }
    size_t n; uint8_t *b = read_file(path, &n); if (!b) return 2;
    mxsb_image im; mxsb_diag d = {0};
    mxsb_err rc = mxsb_decode(b, n, &im, &d);
    if (rc != MXSB_OK) { free(b); return refuse(&d); }
    char hx[65]; mx_sha256_hex(im.digest, hx);
    int si = mxsb_image_find(&im, MXSB_S_SIGNATURE);
    const uint8_t *sig = im.sec[si].data;
    bool zero = true; for (int k = 0; k < 96; ++k) if (sig[k]) zero = false;
    int ret = 0;
    if (zero) {
        bool approved = false;
        for (int k = 0; k < napp; ++k) if (strcmp(approve[k], hx) == 0) approved = true;
        if (napp == 0) { printf("mxsb_emit: V1..V25 ok; UNSIGNED; V20 not checked (no --approve given)\n  digest=%s\n", hx); }
        else if (approved) printf("mxsb_emit: V1..V25 ok; unsigned digest is on the approvals list (V20 ok)\n  digest=%s\n", hx);
        else { fprintf(stderr, "mxsb_emit: refused: MXSB_E_SIGNATURE (V20): unsigned and digest %s not approved\n", hx); ret = 1; }
    } else if (pub) {
        uint8_t raw[32], kid[32];
        if (!pubkey_raw(pub, false, raw)) { fprintf(stderr, "mxsb_emit: cannot read public key %s\n", pub); ret = 2; }
        else {
            mx_sha256_oneshot(raw, 32, kid);
            if (memcmp(kid, sig, 32) != 0) { fprintf(stderr, "mxsb_emit: refused: MXSB_E_UNTRUSTED_KEY (V20): key_id does not match %s\n", pub); ret = 1; }
            else if (!verify_sig(pub, im.digest, sig + 32)) { fprintf(stderr, "mxsb_emit: refused: MXSB_E_SIGNATURE (V20): bad signature\n"); ret = 1; }
            else printf("mxsb_emit: V1..V25 ok; signature valid under %s (V20 ok)\n  digest=%s\n", pub, hx);
        }
    } else {
        char kid[65]; mx_sha256_hex(sig, kid);
        printf("mxsb_emit: V1..V25 ok; signed by key_id=%s; V20 not checked (no --pubkey given)\n  digest=%s\n", kid, hx);
    }
    mxsb_image_free(&im); free(b);
    return ret;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    const char *cmd = argv[1];
    if (!strcmp(cmd, "dump") || !strcmp(cmd, "digest")) {
        if (argc != 3) return usage();
        size_t n; uint8_t *b = read_file(argv[2], &n); if (!b) return 2;
        mxsb_image im; mxsb_diag d = {0};
        mxsb_err rc = mxsb_decode(b, n, &im, &d);
        if (rc == MXSB_E_VERSION && n >= 6 && b[4] == 1) {
            if (!strcmp(cmd, "digest")) { free(b); fprintf(stderr, "mxsb_emit: v1 file has no SHA-256 digest (refused by the loader)\n"); return 1; }
            mxsb_diag d1 = {0}; rc = mxsb_decode_v1(b, n, &im, &d1);
            if (rc != MXSB_OK) { free(b); return refuse(&d1); }
            mxsb_dump(stdout, &im); mxsb_image_free(&im); free(b); return 0;
        }
        if (rc != MXSB_OK) { free(b); return refuse(&d); }
        if (!strcmp(cmd, "digest")) { char hx[65]; mx_sha256_hex(im.digest, hx); printf("%s\n", hx); }
        else mxsb_dump(stdout, &im);
        mxsb_image_free(&im); free(b); return 0;
    }
    if (!strcmp(cmd, "verify")) return do_verify(argc - 2, argv + 2);
    if (!strcmp(cmd, "build") || !strcmp(cmd, "upgrade")) {
        build_args a; memset(&a, 0, sizeof a);
        bool from_v1 = !strcmp(cmd, "upgrade"); const char *v1path = NULL; int i = 2;
        if (from_v1) { if (argc < 3) return usage(); v1path = argv[2]; i = 3; }
        for (; i < argc; ++i) {
            const char *o = argv[i]; const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
#define OPT(name, field) if (!strcmp(o, name)) { if (!v) return usage(); a.field = v; ++i; continue; }
            OPT("--elf", elf) OPT("--text", text) OPT("--data", data) OPT("--manifest", manifest) OPT("-o", out)
            OPT("--sign", sign) OPT("--tier", tier) OPT("--profile", profile)
#undef OPT
            if (!strcmp(o, "--bss")) { if (!v) return usage(); a.bss = strtoull(v, NULL, 0); ++i; continue; }
            if (!strcmp(o, "--entry")) { if (!v) return usage(); a.entry = strtoull(v, NULL, 0); a.have_entry = true; ++i; continue; }
            if (!strcmp(o, "--superop")) { if (!v || a.nsuperop >= 64) return usage(); a.superop[a.nsuperop++] = v; ++i; continue; }
            if (!strcmp(o, "--build-info")) { if (!v || a.nkv >= 64 || !strchr(v, '=') || strchr(v, '\n')) return usage(); a.kv[a.nkv++] = v; ++i; continue; }
            if (!strcmp(o, "--vendor")) { if (!v || a.nvendor >= 16) return usage(); a.vendor[a.nvendor++] = v; ++i; continue; }
            if (!strcmp(o, "--openssl")) { if (!v || v[0] != '/') return usage(); g_openssl = v; ++i; continue; }
            if (!strcmp(o, "--unsigned")) { a.unsigned_ok = true; continue; }
            if (!strcmp(o, "--deterministic-only")) { a.det_only = true; continue; }
            if (!strcmp(o, "--debug-sections")) { a.dbg = true; continue; }
            fprintf(stderr, "mxsb_emit: unknown option %s\n", o); return usage();
        }
        if (a.tier && strcmp(a.tier, "baseline") && strcmp(a.tier, "avx2") && strcmp(a.tier, "avx512") && strcmp(a.tier, "multi")) { fprintf(stderr, "mxsb_emit: --tier must be baseline|avx2|avx512|multi\n"); return 2; }
        return do_build(&a, from_v1, v1path);
    }
    return usage();
}
