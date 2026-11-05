/*
 * mxsb_load.c -- MXSB v2 reference loader. See mxsb_load.h.
 * Contract 2.0.3, section 9a (V1..V28). No allocation, no I/O.
 */
#include "mxsb_load.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- SHA-256 (FIPS 180-4) ----------------------------------------------- */
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(mxsb_ld_sha256_ctx *c, const uint8_t b[64]) {
    uint32_t w[64], a, bb, cc, d, e, f, g, h;
    for (int i = 0; i < 16; ++i) w[i] = ((uint32_t)b[4*i] << 24) | ((uint32_t)b[4*i+1] << 16) | ((uint32_t)b[4*i+2] << 8) | b[4*i+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=c->h[0]; bb=c->h[1]; cc=c->h[2]; d=c->h[3]; e=c->h[4]; f=c->h[5]; g=c->h[6]; h=c->h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25), ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22), mj = (a & bb) ^ (a & cc) ^ (bb & cc);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = bb; bb = a; a = t1 + t2;
    }
    c->h[0]+=a; c->h[1]+=bb; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}
void mxsb_ld_sha256_init(mxsb_ld_sha256_ctx *c) {
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h, iv, sizeof iv); c->nbits = 0; c->buflen = 0;
}
void mxsb_ld_sha256_update(mxsb_ld_sha256_ctx *c, const uint8_t *p, size_t n) {
    c->nbits += (uint64_t)n * 8u;
    while (n) {
        size_t take = 64 - c->buflen; if (take > n) take = n;
        memcpy(c->buf + c->buflen, p, take); c->buflen += (uint32_t)take; p += take; n -= take;
        if (c->buflen == 64) { sha_block(c, c->buf); c->buflen = 0; }
    }
}
void mxsb_ld_sha256_final(mxsb_ld_sha256_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->nbits; uint8_t pad = 0x80;
    mxsb_ld_sha256_update(c, &pad, 1);
    uint8_t z = 0; while (c->buflen != 56) mxsb_ld_sha256_update(c, &z, 1);
    uint8_t lb[8]; for (int i = 0; i < 8; ++i) lb[i] = (uint8_t)(bits >> (56 - 8*i));
    mxsb_ld_sha256_update(c, lb, 8);
    for (int i = 0; i < 8; ++i) { out[4*i] = (uint8_t)(c->h[i] >> 24); out[4*i+1] = (uint8_t)(c->h[i] >> 16); out[4*i+2] = (uint8_t)(c->h[i] >> 8); out[4*i+3] = (uint8_t)c->h[i]; }
}
void mxsb_ld_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    mxsb_ld_sha256_ctx c; mxsb_ld_sha256_init(&c); mxsb_ld_sha256_update(&c, data, len); mxsb_ld_sha256_final(&c, out);
}

/* ---- bounds-checked reads ---------------------------------------------- */
static bool rd_u8(const uint8_t *b, size_t len, uint64_t off, uint8_t *v)   { if (off >= len) return false; *v = b[off]; return true; }
static bool rd_u16(const uint8_t *b, size_t len, uint64_t off, uint16_t *v) { if (off > len || len - off < 2) return false; *v = (uint16_t)(b[off] | (b[off+1] << 8)); return true; }
static bool rd_u32(const uint8_t *b, size_t len, uint64_t off, uint32_t *v) { if (off > len || len - off < 4) return false; *v = (uint32_t)b[off] | ((uint32_t)b[off+1] << 8) | ((uint32_t)b[off+2] << 16) | ((uint32_t)b[off+3] << 24); return true; }
static bool rd_u64(const uint8_t *b, size_t len, uint64_t off, uint64_t *v) { if (off > len || len - off < 8) return false; uint64_t x = 0; for (int i = 7; i >= 0; --i) x = (x << 8) | b[off + (uint64_t)i]; *v = x; return true; }
static bool add_ok(uint64_t a, uint64_t b, uint64_t *r) { if (a > UINT64_MAX - b) return false; *r = a + b; return true; }

/* ---- diagnostics ------------------------------------------------------- */
const char *mxsb_ld_err_name(mxsb_ld_err e) {
    switch (e) {
        case MXSB_LD_OK: return "MXSB_OK";
        case MXSB_LD_E_MAGIC: return "MXSB_E_MAGIC"; case MXSB_LD_E_VERSION: return "MXSB_E_VERSION";
        case MXSB_LD_E_HEADER: return "MXSB_E_HEADER"; case MXSB_LD_E_TABLE: return "MXSB_E_TABLE";
        case MXSB_LD_E_BOUNDS: return "MXSB_E_BOUNDS"; case MXSB_LD_E_OVERLAP: return "MXSB_E_OVERLAP";
        case MXSB_LD_E_MISSING_SECTION: return "MXSB_E_MISSING_SECTION"; case MXSB_LD_E_DUP_SECTION: return "MXSB_E_DUP_SECTION";
        case MXSB_LD_E_ENTRY: return "MXSB_E_ENTRY"; case MXSB_LD_E_DIGEST: return "MXSB_E_DIGEST";
        case MXSB_LD_E_SIGNATURE: return "MXSB_E_SIGNATURE"; case MXSB_LD_E_UNTRUSTED_KEY: return "MXSB_E_UNTRUSTED_KEY";
        case MXSB_LD_E_MANIFEST_SYNTAX: return "MXSB_E_MANIFEST_SYNTAX"; case MXSB_LD_E_MANIFEST_VERSION: return "MXSB_E_MANIFEST_VERSION";
        case MXSB_LD_E_UNKNOWN_PERM: return "MXSB_E_UNKNOWN_PERM"; case MXSB_LD_E_BAD_SCOPE: return "MXSB_E_BAD_SCOPE";
        case MXSB_LD_E_BUILD_ID: return "MXSB_E_BUILD_ID"; case MXSB_LD_E_CAPS_SUMMARY: return "MXSB_E_CAPS_SUMMARY";
        case MXSB_LD_E_NOT_GRANTED: return "MXSB_E_NOT_GRANTED"; case MXSB_LD_E_PADDING: return "MXSB_E_PADDING";
        case MXSB_LD_E_LAYOUT: return "MXSB_E_LAYOUT";
    }
    return "MXSB_E_?";
}
static mxsb_ld_err fail(mxsb_ld_diag *d, mxsb_ld_err e, const char *rule, const char *fmt, ...) {
    if (d) {
        d->err = e; d->rule = rule;
        va_list ap; va_start(ap, fmt); vsnprintf(d->detail, sizeof d->detail, fmt, ap); va_end(ap);
    }
    return e;
}

/* ---- permissions (section 4; names from warden-abi 0.2) ---------------- */
enum scope_kind { SK_NONE, SK_PATH, SK_HOSTPORT, SK_SUPEROP, SK_CLOCK, SK_SIZE, SK_UINT };
static const struct { const char *name; int family; enum scope_kind sk; } perms[] = {
    {"load", 9, SK_NONE}, {"fs.read", 0, SK_PATH}, {"fs.write", 1, SK_PATH}, {"exec", 7, SK_PATH},
    {"net.connect", 2, SK_HOSTPORT}, {"net.listen", 2, SK_HOSTPORT}, {"proc.spawn", 8, SK_NONE},
    {"render", 3, SK_NONE}, {"superop", 4, SK_SUPEROP}, {"clock", 5, SK_CLOCK}, {"random", 6, SK_NONE},
    {"memory.max", -1, SK_SIZE}, {"cpu.max_cores", -1, SK_UINT},
};
static int perm_index(const char *p) { for (size_t i = 0; i < sizeof perms / sizeof perms[0]; ++i) if (strcmp(p, perms[i].name) == 0) return (int)i; return -1; }

static const struct { const char *name; uint32_t id; } superops[] = {   /* provisional: gpu-commands-ref/rcs.h */
    {"create_surface", 0x01}, {"destroy_surface", 0x02}, {"bind_asset", 0x03}, {"upload", 0x04},
    {"set_clip", 0x10}, {"clear", 0x11}, {"fill_rect", 0x12}, {"blit", 0x13}, {"line", 0x14}, {"draw_triangles", 0x15},
    {"begin_frame", 0x20}, {"present", 0x21}, {"fence", 0x22}, {"promise_drop", 0x23},
};
int mxsb_ld_superop_id(const char *name) { for (size_t i = 0; i < sizeof superops / sizeof superops[0]; ++i) if (strcmp(name, superops[i].name) == 0) return (int)superops[i].id; return -1; }
const char *mxsb_ld_superop_name(uint32_t id) { for (size_t i = 0; i < sizeof superops / sizeof superops[0]; ++i) if (superops[i].id == id) return superops[i].name; return NULL; }

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alnum(char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool path_has_dotdot(const char *p) {
    size_t n = strlen(p);
    for (size_t i = 0; i < n;) { size_t j = i; while (j < n && p[j] != '/') ++j; if (j - i == 2 && p[i] == '.' && p[i+1] == '.') return true; i = j + 1; }
    return false;
}
/* host:port; '*' host or port only when allow_wild */
static bool host_port_ok(const char *s, bool allow_wild) {
    const char *c = strrchr(s, ':');
    if (!c || c == s || !c[1]) return false;
    if (strcmp(c + 1, "*") == 0) { if (!allow_wild) return false; }
    else { uint64_t port = 0; for (const char *p = c + 1; *p; ++p) { if (!is_digit(*p)) return false; port = port * 10 + (uint64_t)(*p - '0'); if (port > 65535) return false; } if (port < 1) return false; }
    size_t hl = (size_t)(c - s);
    if (hl == 1 && s[0] == '*') return allow_wild;
    for (size_t i = 0; i < hl; ++i) { char ch = s[i]; if (!(is_alnum(ch) || ch == '.' || ch == '-' || ch == ':' || ch == '[' || ch == ']')) return false; }
    return true;
}
static bool parse_size(const char *s, uint64_t *out) {
    if (!is_digit(s[0])) return false;
    uint64_t v = 0; const char *p = s;
    while (is_digit(*p)) { if (v > UINT64_MAX / 10) return false; v = v * 10 + (uint64_t)(*p - '0'); ++p; }
    uint64_t mul = 1;
    if (*p == 'K') mul = 1ull << 10; else if (*p == 'M') mul = 1ull << 20; else if (*p == 'G') mul = 1ull << 30; else if (*p) return false;
    if (*p && p[1]) return false;
    if (mul > 1 && v > UINT64_MAX / mul) return false;
    *out = v * mul; return true;
}
static bool parse_uint(const char *s, uint64_t *out) {
    if (!is_digit(s[0])) return false;
    uint64_t v = 0; for (const char *p = s; *p; ++p) { if (!is_digit(*p) || v > UINT64_MAX / 10) return false; v = v * 10 + (uint64_t)(*p - '0'); }
    *out = v; return true;
}
static mxsb_ld_err check_scope(int pi, const char *scope, int lineno, mxsb_ld_diag *d) {
    const char *perm = perms[pi].name;
    switch (perms[pi].sk) {
        case SK_NONE: if (scope) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: %s takes no scope", lineno, perm); return MXSB_LD_OK;
        case SK_PATH:
            if (!scope || scope[0] != '/') return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: %s needs an absolute path", lineno, perm);
            if (path_has_dotdot(scope)) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: path contains '..'", lineno);
            return MXSB_LD_OK;
        case SK_HOSTPORT: if (!scope || !host_port_ok(scope, false)) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: %s needs host:port without '*'", lineno, perm); return MXSB_LD_OK;
        case SK_SUPEROP: if (!scope || mxsb_ld_superop_id(scope) < 0) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: unknown super-op", lineno); return MXSB_LD_OK;
        case SK_CLOCK: if (!scope || (strcmp(scope, "coarse") && strcmp(scope, "precise"))) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: clock must be coarse|precise", lineno); return MXSB_LD_OK;
        case SK_SIZE: { uint64_t v; if (!scope || !parse_size(scope, &v)) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: memory.max needs <digits>[K|M|G]", lineno); return MXSB_LD_OK; }
        case SK_UINT: { uint64_t v; if (!scope || !parse_uint(scope, &v) || v == 0) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: cpu.max_cores needs an integer > 0", lineno); return MXSB_LD_OK; }
    }
    return fail(d, MXSB_LD_E_UNKNOWN_PERM, "V22", "line %d: unknown permission", lineno);
}

/* ---- manifest (V21, V22) ----------------------------------------------- */
static int grant_cmp(const mxsb_ld_grant *a, const mxsb_ld_grant *b) {
    char la[MXSB_LD_MANIFEST_LINE + 40], lb[MXSB_LD_MANIFEST_LINE + 40];
    snprintf(la, sizeof la, "%s%s%s", a->perm, a->scope[0] ? "=" : "", a->scope);
    snprintf(lb, sizeof lb, "%s%s%s", b->perm, b->scope[0] ? "=" : "", b->scope);
    return strcmp(la, lb);
}
static mxsb_ld_err manifest_parse(const uint8_t *text, uint64_t len, mxsb_ld_image *im, mxsb_ld_diag *d) {
    /* V21 */
    uint64_t nlines = 0, linelen = 0;
    for (uint64_t i = 0; i < len; ++i) {
        uint8_t c = text[i];
        if (c == 0) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "NUL at %llu", (unsigned long long)i);
        if (c == '\n') { if (++nlines > MXSB_LD_MANIFEST_LINES) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "more than 4096 lines"); linelen = 0; continue; }
        if (c < 0x20 && c != '\t') return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "control byte 0x%02x at %llu", c, (unsigned long long)i);
        if (c >= 0x80) {
            int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
            if (extra < 0 || i + (uint64_t)extra >= len) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "bad UTF-8 at %llu", (unsigned long long)i);
            for (int k = 1; k <= extra; ++k) if ((text[i + (uint64_t)k] & 0xC0) != 0x80) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "bad UTF-8 at %llu", (unsigned long long)i);
            linelen += (uint64_t)extra; i += (uint64_t)extra;
        }
        if (++linelen > MXSB_LD_MANIFEST_LINE) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "line longer than 1024 bytes");
    }
    if (len && text[len-1] != '\n' && nlines + 1 > MXSB_LD_MANIFEST_LINES) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V21", "more than 4096 lines");
    /* V22 */
    bool saw_schema = false, saw_name = false, saw_abi = false, saw_bid = false;
    int lineno = 0; uint64_t pos = 0;
    while (pos < len) {
        uint64_t e = pos; while (e < len && text[e] != '\n') ++e;
        lineno++;
        char line[MXSB_LD_MANIFEST_LINE + 1]; size_t n = 0; bool in_ws = true;
        for (uint64_t i = pos; i < e; ++i) {
            char c = (char)text[i];
            if (c == '#') break;
            if (c == ' ' || c == '\t') { if (!in_ws) { line[n++] = ' '; in_ws = true; } continue; }
            line[n++] = c; in_ws = false;
        }
        while (n && line[n-1] == ' ') --n;
        line[n] = 0; pos = e + 1;
        if (n == 0) continue;
        char *sp = strchr(line, ' '); const char *key = line; char *val = NULL;
        if (sp) { *sp = 0; val = sp + 1; }
        if (!saw_schema) {
            if (strcmp(key, "mxsb-manifest") != 0) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: first line must be 'mxsb-manifest 1'", lineno);
            if (!val || strcmp(val, "1") != 0) return fail(d, MXSB_LD_E_MANIFEST_VERSION, "V22", "line %d: unsupported manifest version", lineno);
            saw_schema = true; continue;
        }
        if (strcmp(key, "mxsb-manifest") == 0) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: duplicate schema line", lineno);
        if (strcmp(key, "name") == 0) {
            if (saw_name || !val || strchr(val, ' ')) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: bad or duplicate name", lineno);
            size_t vl = strlen(val); if (vl < 1 || vl > 64) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: name length", lineno);
            for (const char *p = val; *p; ++p) if (!(is_alnum(*p) || *p == '.' || *p == '_' || *p == '-')) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: bad name character", lineno);
            memcpy(im->name, val, vl + 1); saw_name = true; continue;
        }
        if (strcmp(key, "abi") == 0) {
            if (saw_abi || !val || (strcmp(val, "native") && strcmp(val, "linux-compat"))) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: abi must be native|linux-compat", lineno);
            snprintf(im->abi, sizeof im->abi, "%s", val); saw_abi = true; continue;
        }
        if (strcmp(key, "build_id") == 0) {
            if (saw_bid || !val || strlen(val) != 64) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: build_id must be 64 hex chars", lineno);
            for (const char *p = val; *p; ++p) if (!(is_digit(*p) || (*p >= 'a' && *p <= 'f'))) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: build_id must be lowercase hex", lineno);
            memcpy(im->build_id, val, 65); saw_bid = true; continue;
        }
        if (strcmp(key, "require") == 0) {
            if (!val || strchr(val, ' ')) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: require needs one <perm>[=<scope>]", lineno);
            char *eq = strchr(val, '='); const char *scope = NULL;
            if (eq) { *eq = 0; scope = eq + 1; if (!*scope) return fail(d, MXSB_LD_E_BAD_SCOPE, "V22", "line %d: empty scope", lineno); }
            if (strlen(val) >= sizeof im->require[0].perm) return fail(d, MXSB_LD_E_UNKNOWN_PERM, "V22", "line %d: unknown permission", lineno);
            int pi = perm_index(val);
            if (pi < 0) return fail(d, MXSB_LD_E_UNKNOWN_PERM, "V22", "line %d: unknown permission '%s'", lineno, val);
            mxsb_ld_err rc = check_scope(pi, scope, lineno, d); if (rc) return rc;
            if (im->nrequire >= MXSB_LD_MANIFEST_LINES) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "too many require lines");
            mxsb_ld_grant *g = &im->require[im->nrequire++];
            snprintf(g->perm, sizeof g->perm, "%s", val);
            snprintf(g->scope, sizeof g->scope, "%s", scope ? scope : "");
            continue;
        }
        return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "line %d: unknown key '%s'", lineno, key);
    }
    if (!saw_schema) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "empty manifest");
    if (!saw_name || !saw_abi || !saw_bid) return fail(d, MXSB_LD_E_MANIFEST_SYNTAX, "V22", "name, abi and build_id are required");
    /* normalise: insertion sort (no qsort callback state), drop duplicates */
    for (uint32_t i = 1; i < im->nrequire; ++i) {
        mxsb_ld_grant t = im->require[i]; uint32_t j = i;
        while (j > 0 && grant_cmp(&im->require[j-1], &t) > 0) { im->require[j] = im->require[j-1]; --j; }
        im->require[j] = t;
    }
    uint32_t w = 0;
    for (uint32_t i = 0; i < im->nrequire; ++i) if (w == 0 || grant_cmp(&im->require[w-1], &im->require[i]) != 0) { if (w != i) im->require[w] = im->require[i]; ++w; }
    im->nrequire = w;
    uint64_t caps = 0;
    for (uint32_t i = 0; i < im->nrequire; ++i) { int pi = perm_index(im->require[i].perm); if (pi >= 0 && perms[pi].family >= 0) caps |= 1ull << perms[pi].family; }
    im->caps_summary = caps;
    return MXSB_LD_OK;
}

/* ---- V26: attenuation against the container manifest ------------------- */
static bool prefix_ok(const char *want, const char *granted) {
    size_t gl = strlen(granted);
    if (strcmp(granted, "/") == 0) return true;
    if (strncmp(want, granted, gl) != 0) return false;
    return want[gl] == 0 || want[gl] == '/';
}
static bool hostport_match(const char *want, const char *granted) {
    const char *wc = strrchr(want, ':'), *gc = strrchr(granted, ':');
    if (!wc || !gc) return false;
    size_t wh = (size_t)(wc - want), gh = (size_t)(gc - granted);
    bool host_ok = (gh == 1 && granted[0] == '*') || (wh == gh && strncmp(want, granted, wh) == 0);
    bool port_ok = strcmp(gc + 1, "*") == 0 || strcmp(wc + 1, gc + 1) == 0;
    return host_ok && port_ok;
}
static mxsb_ld_err check_grant(const mxsb_ld_grant *g, const mxsb_ld_container *c, mxsb_ld_diag *d) {
    const char *p = g->perm, *s = g->scope;
    if (strcmp(p, "load") == 0) return MXSB_LD_OK;
    if (strcmp(p, "fs.read") == 0 || strcmp(p, "fs.write") == 0 || strcmp(p, "exec") == 0) {
        for (uint32_t i = 0; i < c->ngrant; ++i) if (strcmp(c->grant[i].perm, p) == 0 && prefix_ok(s, c->grant[i].scope)) return MXSB_LD_OK;
        return fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "%s=%s not under any container grant", p, s);
    }
    if (strcmp(p, "net.connect") == 0) {
        for (uint32_t i = 0; i < c->ngrant; ++i) if (strcmp(c->grant[i].perm, p) == 0 && hostport_match(s, c->grant[i].scope)) return MXSB_LD_OK;
        return fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "net.connect=%s not granted", s);
    }
    if (strcmp(p, "net.listen") == 0) {
        for (uint32_t i = 0; i < c->ngrant; ++i) if (strcmp(c->grant[i].perm, p) == 0 && strcmp(c->grant[i].scope, s) == 0) return MXSB_LD_OK;
        return fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "net.listen=%s not granted", s);
    }
    if (strcmp(p, "superop") == 0) {
        for (uint32_t i = 0; i < c->ngrant; ++i) if (strcmp(c->grant[i].perm, p) == 0 && strcmp(c->grant[i].scope, s) == 0) return MXSB_LD_OK;
        return fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "superop=%s not in container superops", s);
    }
    if (strcmp(p, "proc.spawn") == 0) return c->process_spawn_max > 0 ? MXSB_LD_OK : fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "proc.spawn: container process.spawn.max is 0");
    if (strcmp(p, "render") == 0) return c->render ? MXSB_LD_OK : fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "render not granted");
    if (strcmp(p, "random") == 0) return c->random ? MXSB_LD_OK : fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "random not granted (no default)");
    if (strcmp(p, "clock") == 0) {
        if (strcmp(c->clock, "precise") == 0) return MXSB_LD_OK;
        if (strcmp(c->clock, "coarse") == 0 && strcmp(s, "coarse") == 0) return MXSB_LD_OK;
        return fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "clock=%s exceeds container clock '%s'", s, c->clock);
    }
    if (strcmp(p, "memory.max") == 0) { uint64_t v = 0; parse_size(s, &v); return (c->memory_max && v <= c->memory_max) ? MXSB_LD_OK : fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "memory.max=%s above container budget", s); }
    if (strcmp(p, "cpu.max_cores") == 0) { uint64_t v = 0; parse_uint(s, &v); return (c->cpu_max_cores && v <= c->cpu_max_cores) ? MXSB_LD_OK : fail(d, MXSB_LD_E_NOT_GRANTED, "V26", "cpu.max_cores=%s above container budget", s); }
    return fail(d, MXSB_LD_E_UNKNOWN_PERM, "V26", "unknown permission %s", p);
}

/* ---- the loader check -------------------------------------------------- */
typedef struct { uint32_t type, flags; uint64_t offset, size; } tentry;

mxsb_ld_err mxsb_ld_check(const uint8_t *buf, size_t len, const mxsb_ld_policy *pol, mxsb_ld_image *im, mxsb_ld_diag *d) {
    mxsb_ld_err rc;
    if (d) { d->err = MXSB_LD_OK; d->rule = "-"; d->detail[0] = 0; }
    memset(im, 0, sizeof *im);
#define FAIL(e, r, ...) do { rc = fail(d, e, r, __VA_ARGS__); goto bad; } while (0)
    /* V1 */
    if (!buf || len < MXSB_LD_HEADER) FAIL(MXSB_LD_E_HEADER, "V1", "file shorter than 64 bytes (%zu)", len);
    /* V2 */
    if (memcmp(buf, "MXSB", 4) != 0) FAIL(MXSB_LD_E_MAGIC, "V2", "bad magic");
    /* V3 */
    uint8_t major, minor; rd_u8(buf, len, 4, &major); rd_u8(buf, len, 5, &minor);
    if (major != 2) FAIL(MXSB_LD_E_VERSION, "V3", "ver_major %u (loader accepts 2 only; v1 files must be upgraded)", major);
    /* V4 */
    uint16_t header_size; rd_u16(buf, len, 6, &header_size);
    if (header_size < MXSB_LD_HEADER || header_size > MXSB_LD_HEADER_MAX || header_size > len) FAIL(MXSB_LD_E_HEADER, "V4", "header_size %u", header_size);
    /* V5 */
    uint64_t file_size; rd_u64(buf, len, 40, &file_size);
    if (file_size != (uint64_t)len) FAIL(MXSB_LD_E_HEADER, "V5", "file_size %llu != length %zu", (unsigned long long)file_size, len);
    /* V6 */
    uint32_t flags; rd_u32(buf, len, 8, &flags);
    if (flags & MXSB_LD_FLAGS_RESERVED) FAIL(MXSB_LD_E_HEADER, "V6", "reserved flag bits set (0x%08x)", flags);
    for (uint64_t k = 48; k < 64; ++k) if (buf[k]) FAIL(MXSB_LD_E_HEADER, "V6", "header reserved byte %llu not zero", (unsigned long long)k);
    /* V7 */
    uint32_t nsec; rd_u32(buf, len, 12, &nsec);
    if (nsec < 1 || nsec > MXSB_LD_SECTIONS_MAX) FAIL(MXSB_LD_E_TABLE, "V7", "section_count %u", nsec);
    /* V8 */
    uint64_t table_off; rd_u64(buf, len, 16, &table_off);
    if (table_off % 16 != 0 || table_off < header_size) FAIL(MXSB_LD_E_TABLE, "V8", "section_table_off %llu", (unsigned long long)table_off);
    /* V9 */
    uint64_t table_bytes = (uint64_t)nsec * MXSB_LD_ENTRY, table_end;
    if (!add_ok(table_off, table_bytes, &table_end) || table_end > len) FAIL(MXSB_LD_E_TABLE, "V9", "section table past end of file");
    /* read table */
    tentry t[MXSB_LD_SECTIONS_MAX];
    for (uint32_t i = 0; i < nsec; ++i) {
        uint64_t eo = table_off + (uint64_t)i * MXSB_LD_ENTRY, resv;
        rd_u32(buf, len, eo, &t[i].type); rd_u32(buf, len, eo + 4, &t[i].flags);
        rd_u64(buf, len, eo + 8, &t[i].offset); rd_u64(buf, len, eo + 16, &t[i].size); rd_u64(buf, len, eo + 24, &resv);
        /* V10 */
        if (resv) FAIL(MXSB_LD_E_TABLE, "V10", "entry %u reserved not zero", i);
        uint32_t want = 0;
        switch (t[i].type) { case MXSB_LD_S_TEXT: want = MXSB_LD_SF_R | MXSB_LD_SF_X; break; case MXSB_LD_S_DATA: case MXSB_LD_S_BSS: want = MXSB_LD_SF_R | MXSB_LD_SF_W; break; default: want = 0; }
        if (t[i].flags != want) FAIL(MXSB_LD_E_TABLE, "V10", "entry %u (type %u) flags 0x%x, want 0x%x", i, t[i].type, t[i].flags, want);
        if (t[i].type == 0) FAIL(MXSB_LD_E_TABLE, "V10", "entry %u section type 0", i);
        /* types 8..0x7FFF are reserved for later minors and skipped (section 7); vendor types are skipped */
        if (t[i].type == MXSB_LD_S_BSS) {
            /* V12 */
            if (t[i].offset != 0) FAIL(MXSB_LD_E_BOUNDS, "V12", "BSS offset %llu != 0", (unsigned long long)t[i].offset);
            if (t[i].size > MXSB_LD_BSS_MAX) FAIL(MXSB_LD_E_BOUNDS, "V12", "BSS size > 1 GiB");
        } else {
            /* V11 */
            if (t[i].offset % 16 != 0) FAIL(MXSB_LD_E_BOUNDS, "V11", "entry %u offset %llu not 16-aligned", i, (unsigned long long)t[i].offset);
            if (t[i].offset < table_end) FAIL(MXSB_LD_E_BOUNDS, "V11", "entry %u offset %llu inside header/table", i, (unsigned long long)t[i].offset);
            if (t[i].size < 1) FAIL(MXSB_LD_E_BOUNDS, "V11", "entry %u size 0", i);
            if (t[i].size > len || t[i].offset > (uint64_t)len - t[i].size) FAIL(MXSB_LD_E_BOUNDS, "V11", "entry %u range %llu+%llu past end", i, (unsigned long long)t[i].offset, (unsigned long long)t[i].size);
        }
    }
    /* V13: strictly increasing offsets, no overlap (non-BSS) */
    {
        int64_t prev = -1;
        for (uint32_t i = 0; i < nsec; ++i) {
            if (t[i].type == MXSB_LD_S_BSS) continue;
            if (prev >= 0) {
                if (t[i].offset <= t[prev].offset) FAIL(MXSB_LD_E_OVERLAP, "V13", "table not sorted by offset at entry %u", i);
                if (t[prev].offset + t[prev].size > t[i].offset) FAIL(MXSB_LD_E_OVERLAP, "V13", "entry %u overlaps entry %u", i, (unsigned)prev);
            }
            prev = (int64_t)i;
        }
    }
    /* V27: padding zero (header extension, header..table, between sections, trailing) */
    {
        for (uint64_t k = MXSB_LD_HEADER; k < header_size; ++k) if (buf[k]) FAIL(MXSB_LD_E_PADDING, "V27", "header extension byte %llu not zero", (unsigned long long)k);
        for (uint64_t k = header_size; k < table_off; ++k) if (buf[k]) FAIL(MXSB_LD_E_PADDING, "V27", "padding byte %llu not zero", (unsigned long long)k);
        uint64_t cursor = table_end;
        for (uint32_t i = 0; i < nsec; ++i) {
            if (t[i].type == MXSB_LD_S_BSS) continue;
            for (uint64_t k = cursor; k < t[i].offset; ++k) if (buf[k]) FAIL(MXSB_LD_E_PADDING, "V27", "padding byte %llu not zero", (unsigned long long)k);
            cursor = t[i].offset + t[i].size;
        }
        for (uint64_t k = cursor; k < len; ++k) if (buf[k]) FAIL(MXSB_LD_E_PADDING, "V27", "trailing byte %llu not zero", (unsigned long long)k);
    }
    /* V14 */
    int idx[8]; for (int k = 0; k < 8; ++k) idx[k] = -1;
    for (uint32_t i = 0; i < nsec; ++i) if (t[i].type >= 1 && t[i].type <= 7) { if (idx[t[i].type] >= 0) FAIL(MXSB_LD_E_DUP_SECTION, "V14", "section type %u appears twice", t[i].type); idx[t[i].type] = (int)i; }
    /* V15 */
    if (idx[MXSB_LD_S_MANIFEST] < 0 || idx[MXSB_LD_S_TEXT] < 0 || idx[MXSB_LD_S_SIGNATURE] < 0) FAIL(MXSB_LD_E_MISSING_SECTION, "V15", "MANIFEST, TEXT and SIGNATURE are required");
    const tentry *tx = &t[idx[MXSB_LD_S_TEXT]], *tm = &t[idx[MXSB_LD_S_MANIFEST]], *tsig = &t[idx[MXSB_LD_S_SIGNATURE]];
    const tentry *td = idx[MXSB_LD_S_DATA] >= 0 ? &t[idx[MXSB_LD_S_DATA]] : NULL;
    const tentry *tb = idx[MXSB_LD_S_BSS] >= 0 ? &t[idx[MXSB_LD_S_BSS]] : NULL;
    const tentry *tso = idx[MXSB_LD_S_SUPEROPS] >= 0 ? &t[idx[MXSB_LD_S_SUPEROPS]] : NULL;
    /* V28 */
    if ((td || tb) && tx->size % MXSB_LD_PAGE != 0) FAIL(MXSB_LD_E_LAYOUT, "V28", "TEXT.size %llu not a page multiple with DATA/BSS present", (unsigned long long)tx->size);
    /* V16 */
    if (tsig->size != MXSB_LD_SIGNATURE_SIZE) FAIL(MXSB_LD_E_TABLE, "V16", "SIGNATURE size %llu != 96", (unsigned long long)tsig->size);
    if (tso && tso->size % 4 != 0) FAIL(MXSB_LD_E_TABLE, "V16", "SUPEROPS size not a multiple of 4");
    if (tso && tso->size / 4 > 256) FAIL(MXSB_LD_E_TABLE, "V16", "SUPEROPS has more than 256 ids (loader limit)");
    if (tm->size > MXSB_LD_MANIFEST_MAX) FAIL(MXSB_LD_E_TABLE, "V16", "MANIFEST larger than 64 KiB");
    if (idx[MXSB_LD_S_BUILD_INFO] >= 0 && t[idx[MXSB_LD_S_BUILD_INFO]].size > MXSB_LD_BUILD_INFO_MAX) FAIL(MXSB_LD_E_TABLE, "V16", "BUILD_INFO larger than 64 KiB");
    /* V17 */
    uint64_t entry_off; rd_u64(buf, len, 24, &entry_off);
    if (entry_off < tx->offset || entry_off >= tx->offset + tx->size) FAIL(MXSB_LD_E_ENTRY, "V17", "entry_off %llu outside TEXT", (unsigned long long)entry_off);
    /* V18 */
    uint64_t total = tx->size, tmp;
    if (td && !add_ok(total, td->size, &total)) FAIL(MXSB_LD_E_BOUNDS, "V18", "mapped size overflow");
    if (tb && !add_ok(total, tb->size, &total)) FAIL(MXSB_LD_E_BOUNDS, "V18", "mapped size overflow");
    if (total > MXSB_LD_MAPPED_MAX) FAIL(MXSB_LD_E_BOUNDS, "V18", "mapped size %llu > 4 GiB", (unsigned long long)total);
    if (!add_ok(total, MXSB_LD_PAGE - 1, &tmp)) FAIL(MXSB_LD_E_BOUNDS, "V18", "mapped size overflow");
    im->mapped_total = tmp & ~(MXSB_LD_PAGE - 1);
    /* V19: digest over the file with the SIGNATURE bytes removed */
    {
        mxsb_ld_sha256_ctx c; mxsb_ld_sha256_init(&c);
        mxsb_ld_sha256_update(&c, buf, (size_t)tsig->offset);
        mxsb_ld_sha256_update(&c, buf + tsig->offset + tsig->size, (size_t)(len - (tsig->offset + tsig->size)));
        mxsb_ld_sha256_final(&c, im->digest);
    }
    /* V20 */
    memcpy(im->key_id, buf + tsig->offset, 32);
    if (pol && pol->digest_only) { im->text_off = tx->offset; im->text_size = tx->size; return MXSB_LD_OK; }
    if (!pol || !pol->skip_signature) {
        bool all_zero = true; for (uint32_t k = 0; k < 96; ++k) if (buf[tsig->offset + k]) { all_zero = false; break; }
        if (all_zero) {
            bool allowed = pol && pol->container && pol->container->allow_unsigned;
            if (!allowed) FAIL(MXSB_LD_E_SIGNATURE, "V20", "unsigned binary and container does not allow_unsigned");
            bool approved = false;
            for (uint32_t k = 0; pol && k < pol->napproved; ++k) if (memcmp(pol->approved_digests[k], im->digest, 32) == 0) { approved = true; break; }
            if (!approved) FAIL(MXSB_LD_E_UNTRUSTED_KEY, "V20", "unsigned binary: digest not on the approvals list");
        } else {
            bool trusted = false;
            for (uint32_t k = 0; pol && k < pol->ntrusted; ++k) if (memcmp(pol->trusted_key_ids[k], im->key_id, 32) == 0) { trusted = true; break; }
            if (!trusted) FAIL(MXSB_LD_E_UNTRUSTED_KEY, "V20", "key id not in the trust list");
            if (!pol->verify) FAIL(MXSB_LD_E_SIGNATURE, "V20", "no signature verifier configured");
            if (!pol->verify(pol->verify_ctx, im->key_id, im->digest, buf + tsig->offset + 32)) FAIL(MXSB_LD_E_SIGNATURE, "V20", "Ed25519 signature invalid");
            im->signed_ok = true;
        }
    }
    /* V21, V22 */
    rc = manifest_parse(buf + tm->offset, tm->size, im, d); if (rc) goto bad;
    /* V23 */
    {
        uint8_t dg[32]; char hex[65]; mxsb_ld_sha256_ctx c; mxsb_ld_sha256_init(&c);
        mxsb_ld_sha256_update(&c, buf + tx->offset, (size_t)tx->size);
        if (td) mxsb_ld_sha256_update(&c, buf + td->offset, (size_t)td->size);
        mxsb_ld_sha256_final(&c, dg);
        for (int k = 0; k < 32; ++k) snprintf(hex + 2*k, 3, "%02x", dg[k]);
        if (strcmp(hex, im->build_id) != 0) FAIL(MXSB_LD_E_BUILD_ID, "V23", "build_id does not match SHA-256(TEXT||DATA)");
    }
    /* V24 */
    uint64_t caps; rd_u64(buf, len, 32, &caps);
    if (caps & MXSB_LD_CAPS_RESERVED) FAIL(MXSB_LD_E_CAPS_SUMMARY, "V24", "reserved caps_summary bits set");
    if (caps != im->caps_summary) FAIL(MXSB_LD_E_CAPS_SUMMARY, "V24", "caps_summary 0x%llx != manifest 0x%llx", (unsigned long long)caps, (unsigned long long)im->caps_summary);
    /* V25 */
    if (tso) {
        for (uint64_t k = 0; k < tso->size; k += 4) {
            uint32_t id; rd_u32(buf, len, tso->offset + k, &id);
            const char *nm = mxsb_ld_superop_name(id);
            bool found = false;
            if (nm) for (uint32_t i = 0; i < im->nrequire; ++i) if (strcmp(im->require[i].perm, "superop") == 0 && strcmp(im->require[i].scope, nm) == 0) { found = true; break; }
            if (!found) FAIL(MXSB_LD_E_NOT_GRANTED, "V25", "SUPEROPS id %u (%s) has no require line", id, nm ? nm : "unknown");
            im->superop_ids[im->nsuperops++] = id;
        }
    }
    /* V26 */
    if (pol && pol->container) for (uint32_t i = 0; i < im->nrequire; ++i) { rc = check_grant(&im->require[i], pol->container, d); if (rc) goto bad; }
    /* fill the image */
    im->text_off = tx->offset; im->text_size = tx->size;
    if (td) { im->data_off = td->offset; im->data_size = td->size; }
    if (tb) im->bss_size = tb->size;
    im->entry_rel = entry_off - tx->offset; im->flags = flags; im->caps_summary = caps;
    return MXSB_LD_OK;
bad:
    memset(im, 0, sizeof *im);
    return rc;
#undef FAIL
}

/* ---- warden.toml [grants] subset --------------------------------------- */
static void trim(char *s) {
    size_t n = strlen(s); while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = 0;
    size_t i = 0; while (s[i] == ' ' || s[i] == '\t') ++i; if (i) memmove(s, s + i, n - i + 1);
}
static bool add_grant(mxsb_ld_container *c, const char *perm, const char *scope) {
    if (c->ngrant >= sizeof c->grant / sizeof c->grant[0]) return false;
    snprintf(c->grant[c->ngrant].perm, sizeof c->grant[0].perm, "%s", perm);
    snprintf(c->grant[c->ngrant].scope, sizeof c->grant[0].scope, "%s", scope);
    c->ngrant++; return true;
}
/* parse ["a", "b"] into add_grant(perm, ...) */
static bool parse_list(mxsb_ld_container *c, const char *perm, const char *v, char err[128]) {
    if (v[0] != '[') { snprintf(err, 128, "%s: expected a list", perm); return false; }
    const char *p = v + 1;
    for (;;) {
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']') return true;
        if (*p != '"') { snprintf(err, 128, "%s: expected a quoted string", perm); return false; }
        const char *q = strchr(p + 1, '"'); if (!q) { snprintf(err, 128, "%s: unterminated string", perm); return false; }
        char item[MXSB_LD_MANIFEST_LINE]; size_t n = (size_t)(q - p - 1); if (n >= sizeof item) n = sizeof item - 1;
        memcpy(item, p + 1, n); item[n] = 0;
        bool ok = true;
        if (strcmp(perm, "superop") == 0) ok = mxsb_ld_superop_id(item) >= 0;
        else if (strcmp(perm, "net.connect") == 0 || strcmp(perm, "net.listen") == 0) ok = host_port_ok(item, true);
        else ok = item[0] == '/' && !path_has_dotdot(item);
        if (!ok) { snprintf(err, 128, "%.20s: bad scope '%.60s'", perm, item); return false; }
        if (!add_grant(c, perm, item)) { snprintf(err, 128, "too many grants"); return false; }
        p = q + 1;
    }
}
/* find `key = <token>` inside an inline table { ... }; token copied without quotes */
static bool inline_get(const char *tbl, const char *key, char out[64]) {
    const char *p = tbl;
    size_t kl = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        bool start_ok = (p == tbl) || p[-1] == '{' || p[-1] == ' ' || p[-1] == ',';
        const char *q = p + kl; while (*q == ' ') ++q;
        if (start_ok && *q == '=') {
            ++q; while (*q == ' ') ++q;
            bool quoted = *q == '"'; if (quoted) ++q;
            size_t n = 0; while (*q && n < 63 && *q != (quoted ? '"' : ',') && *q != '}' && (quoted || *q != ' ')) out[n++] = *q++;
            out[n] = 0; return true;
        }
        p += kl;
    }
    return false;
}
bool mxsb_ld_container_parse(const char *text, size_t len, mxsb_ld_container *c, char err[128]) {
    memset(c, 0, sizeof *c); err[0] = 0;
    size_t pos = 0; int lineno = 0;
    while (pos < len) {
        size_t e = pos; while (e < len && text[e] != '\n') ++e;
        char line[2048]; size_t n = e - pos; if (n >= sizeof line) { snprintf(err, 128, "line %d too long", lineno + 1); return false; }
        memcpy(line, text + pos, n); line[n] = 0; pos = e + 1; lineno++;
        char *hash = NULL;
        { bool inq = false; for (char *q = line; *q; ++q) { if (*q == '"') inq = !inq; else if (*q == '#' && !inq) { hash = q; break; } } }
        if (hash) *hash = 0;
        trim(line);
        if (!line[0] || line[0] == '[') continue;      /* section headers ignored: keys are unique */
        char *eq = strchr(line, '='); if (!eq) { snprintf(err, 128, "line %d: expected key = value", lineno); return false; }
        *eq = 0; char *key = line, *val = eq + 1; trim(key); trim(val);
        if (!strcmp(key, "fs.read") || !strcmp(key, "fs.write") || !strcmp(key, "exec") || !strcmp(key, "net.connect") || !strcmp(key, "net.listen")) {
            if (!parse_list(c, key, val, err)) return false;
        } else if (!strcmp(key, "superops")) {
            if (!parse_list(c, "superop", val, err)) return false;
        } else if (!strcmp(key, "render") || !strcmp(key, "random") || !strcmp(key, "allow_unsigned")) {
            bool b; if (!strcmp(val, "true")) b = true; else if (!strcmp(val, "false")) b = false; else { snprintf(err, 128, "line %d: %.40s must be true|false", lineno, key); return false; }
            if (key[0] == 'r' && key[1] == 'e') c->render = b; else if (key[0] == 'r') c->random = b; else c->allow_unsigned = b;
        } else if (!strcmp(key, "clock")) {
            char v[64]; size_t vl = strlen(val);
            if (vl < 2 || val[0] != '"' || val[vl-1] != '"') { snprintf(err, 128, "line %d: clock must be a quoted string", lineno); return false; }
            memcpy(v, val + 1, vl - 2); v[vl-2] = 0;
            if (strcmp(v, "coarse") && strcmp(v, "precise")) { snprintf(err, 128, "line %d: clock must be coarse|precise", lineno); return false; }
            snprintf(c->clock, sizeof c->clock, "%.7s", v);
        } else if (!strcmp(key, "memory")) {
            char v[64]; if (!inline_get(val, "max", v) || !parse_size(v, &c->memory_max)) { snprintf(err, 128, "line %d: memory needs max = \"<size>\"", lineno); return false; }
        } else if (!strcmp(key, "cpu")) {
            char v[64]; uint64_t x; if (!inline_get(val, "max_cores", v) || !parse_uint(v, &x) || x > UINT32_MAX) { snprintf(err, 128, "line %d: cpu needs max_cores = <n>", lineno); return false; }
            c->cpu_max_cores = (uint32_t)x;
        } else if (!strcmp(key, "process.spawn")) {
            char v[64]; uint64_t x; if (!inline_get(val, "max", v) || !parse_uint(v, &x) || x > UINT32_MAX) { snprintf(err, 128, "line %d: process.spawn needs max = <n>", lineno); return false; }
            c->process_spawn_max = (uint32_t)x;
        } /* name, abi, syscalls and anything else: not the loader's concern */
    }
    return true;
}
