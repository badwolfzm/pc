/*
 * mxsb_check -- reference loader CLI.
 *
 *   mxsb_check FILE [--container warden.toml] [--approve HEX]... [--trust KEYID_HEX]...
 *                   [--pubkey PUB.pem] [--openssl /abs/path] [--structural] [--dump]
 *
 * Exit 0: admitted (prints "MXSB_OK" and the image summary).
 * Exit 1: refused (prints "<error> <rule>: <detail>").
 * Exit 2: usage or I/O error.
 *
 * --structural  runs V1..V19 and V21..V25 only (no V20, no V26): what a
 *               tool uses to inspect a file. A real load always has a
 *               container and a trust list.
 * --pubkey      Ed25519 verification through /usr/bin/openssl (fork/exec with
 *               an argv, no shell). The key id SHA-256(raw public key) is
 *               derived from the PEM and added to the trust list.
 */
#define _GNU_SOURCE
#include "mxsb_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

static const char *g_openssl = "/usr/bin/openssl";

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f); if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f); b[n] = 0; *len = (size_t)n; return b;
}
static bool unhex(const char *s, uint8_t *out, size_t n) {
    if (strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < n; ++i) { unsigned v; if (sscanf(s + 2*i, "%2x", &v) != 1) return false; out[i] = (uint8_t)v; }
    return true;
}
static int run(const char *const argv[]) {
    pid_t p = fork(); if (p < 0) return -1;
    if (p == 0) { int dn = open("/dev/null", O_RDWR); if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); } execv(argv[0], (char *const *)argv); _exit(127); }
    int st = 0; if (waitpid(p, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
typedef struct { const char *pubpem; } vctx;
static bool verify_openssl(void *ctx, const uint8_t key_id[32], const uint8_t digest[32], const uint8_t sig[64]) {
    (void)key_id;
    vctx *v = ctx;
    char dpath[] = "/tmp/mxsb_dXXXXXX", spath[] = "/tmp/mxsb_sXXXXXX";
    int fd = mkstemp(dpath); if (fd < 0) return false; if (write(fd, digest, 32) != 32) { close(fd); return false; } close(fd);
    fd = mkstemp(spath); if (fd < 0) { unlink(dpath); return false; } if (write(fd, sig, 64) != 64) { close(fd); unlink(dpath); return false; } close(fd);
    const char *argv[] = { g_openssl, "pkeyutl", "-verify", "-pubin", "-inkey", v->pubpem, "-rawin", "-in", dpath, "-sigfile", spath, NULL };
    int rc = run(argv);
    unlink(dpath); unlink(spath);
    return rc == 0;
}
/* key id = SHA-256(raw 32-byte Ed25519 public key): the raw key is the last 32 bytes of the DER SPKI */
static bool key_id_from_pem(const char *pem, uint8_t out[32]) {
    char der[] = "/tmp/mxsb_kXXXXXX"; int fd = mkstemp(der); if (fd < 0) return false; close(fd);
    const char *argv[] = { g_openssl, "pkey", "-pubin", "-in", pem, "-outform", "DER", "-out", der, NULL };
    if (run(argv) != 0) { unlink(der); return false; }
    size_t n; uint8_t *b = slurp(der, &n); unlink(der);
    if (!b || n < 32) { free(b); return false; }
    mxsb_ld_sha256(b + n - 32, 32, out); free(b); return true;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: mxsb_check FILE [--container warden.toml] [--approve HEX]... [--trust KEYID]... [--pubkey PUB.pem] [--openssl PATH] [--structural] [--dump]\n"); return 2; }
    const char *path = argv[1], *ctoml = NULL, *pubpem = NULL;
    static uint8_t approved[64][32], trusted[64][32];
    uint32_t napp = 0, ntr = 0; bool structural = false, dump = false;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--container") && i + 1 < argc) ctoml = argv[++i];
        else if (!strcmp(argv[i], "--approve") && i + 1 < argc && napp < 64) { if (!unhex(argv[++i], approved[napp++], 32)) { fprintf(stderr, "bad --approve hex\n"); return 2; } }
        else if (!strcmp(argv[i], "--trust") && i + 1 < argc && ntr < 64) { if (!unhex(argv[++i], trusted[ntr++], 32)) { fprintf(stderr, "bad --trust hex\n"); return 2; } }
        else if (!strcmp(argv[i], "--pubkey") && i + 1 < argc) pubpem = argv[++i];
        else if (!strcmp(argv[i], "--openssl") && i + 1 < argc) g_openssl = argv[++i];
        else if (!strcmp(argv[i], "--structural")) structural = true;
        else if (!strcmp(argv[i], "--dump")) dump = true;
        else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 2; }
    }
    if (g_openssl[0] != '/') { fprintf(stderr, "--openssl must be an absolute path\n"); return 2; }
    size_t len; uint8_t *buf = slurp(path, &len);
    if (!buf) { fprintf(stderr, "cannot read %s\n", path); return 2; }
    mxsb_ld_container cont; char cerr[128];
    mxsb_ld_policy pol; memset(&pol, 0, sizeof pol);
    if (ctoml) {
        size_t cl; uint8_t *ct = slurp(ctoml, &cl); if (!ct) { fprintf(stderr, "cannot read %s\n", ctoml); return 2; }
        if (!mxsb_ld_container_parse((const char *)ct, cl, &cont, cerr)) { fprintf(stderr, "container manifest: %s\n", cerr); return 2; }
        free(ct); pol.container = &cont;
    }
    vctx vc = { pubpem };
    if (pubpem) {
        if (ntr < 64 && key_id_from_pem(pubpem, trusted[ntr])) ntr++; else { fprintf(stderr, "cannot derive key id from %s\n", pubpem); return 2; }
        pol.verify = verify_openssl; pol.verify_ctx = &vc;
    }
    pol.approved_digests = approved; pol.napproved = napp;
    pol.trusted_key_ids = trusted; pol.ntrusted = ntr;
    pol.skip_signature = structural;
    if (structural) pol.container = NULL;
    static mxsb_ld_image im; mxsb_ld_diag d;
    mxsb_ld_err rc = mxsb_ld_check(buf, len, &pol, &im, &d);
    if (rc != MXSB_LD_OK) { printf("%s %s: %s\n", mxsb_ld_err_name(rc), d.rule, d.detail); free(buf); return 1; }
    char hex[65]; for (int k = 0; k < 32; ++k) snprintf(hex + 2*k, 3, "%02x", im.digest[k]);
    printf("MXSB_OK%s digest=%s\n", structural ? " (structural: V20/V26 skipped)" : im.signed_ok ? " signed" : " unsigned+approved", hex);
    if (dump) {
        printf("  name=%s abi=%s build_id=%s\n  text=%llu+%llu data=%llu+%llu bss=%llu entry_rel=%llu mapped=%llu flags=0x%x caps=0x%llx\n",
               im.name, im.abi, im.build_id, (unsigned long long)im.text_off, (unsigned long long)im.text_size,
               (unsigned long long)im.data_off, (unsigned long long)im.data_size, (unsigned long long)im.bss_size,
               (unsigned long long)im.entry_rel, (unsigned long long)im.mapped_total, im.flags, (unsigned long long)im.caps_summary);
        for (uint32_t i = 0; i < im.nrequire; ++i) printf("  require %s%s%s\n", im.require[i].perm, im.require[i].scope[0] ? "=" : "", im.require[i].scope);
        for (uint32_t i = 0; i < im.nsuperops; ++i) printf("  superop id 0x%x (%s)\n", im.superop_ids[i], mxsb_ld_superop_name(im.superop_ids[i]));
    }
    free(buf); return 0;
}
