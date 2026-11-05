/*
 * mxsb_contract.c -- harness-side test of an MXSB v2 validator against
 * matrixos/contracts/mxsb-manifest.md section 9a (security review finding 7).
 *
 * Independence: the valid file is HAND-BUILT here from the byte tables in
 * sections 3.1-3.3 and 4 of the contract, not through the module's image API.
 * If it validates, the validator agrees with the contract on the layout; each
 * mutation then expects the error code the 9a table names.
 *
 * Two backends, same mutations, must agree:
 *   codegen:  cc -std=c11 -O1 -g -fsanitize=address,undefined -I<codegen/mxsb> mxsb_contract.c <codegen/mxsb>/mxsb.c
 *   mxsb-ref: cc ... -DMXSB_CONTRACT_REF -I<contracts/mxsb-ref> mxsb_contract.c <contracts/mxsb-ref>/mxsb_load.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MXSB_CONTRACT_REF
/* backend 2: the contract owner's reference loader, contracts/mxsb-ref/mxsb_load.c.
 * Error numbers are the schema's in both backends, so the MXSB_E_* names map 1:1. */
#include "mxsb_load.h"
typedef mxsb_ld_err mxsb_err;
#define MXSB_OK MXSB_LD_OK
#define MXSB_E_MAGIC MXSB_LD_E_MAGIC
#define MXSB_E_VERSION MXSB_LD_E_VERSION
#define MXSB_E_HEADER MXSB_LD_E_HEADER
#define MXSB_E_TABLE MXSB_LD_E_TABLE
#define MXSB_E_BOUNDS MXSB_LD_E_BOUNDS
#define MXSB_E_OVERLAP MXSB_LD_E_OVERLAP
#define MXSB_E_MISSING_SECTION MXSB_LD_E_MISSING_SECTION
#define MXSB_E_DUP_SECTION MXSB_LD_E_DUP_SECTION
#define MXSB_E_ENTRY MXSB_LD_E_ENTRY
#define MXSB_E_MANIFEST_SYNTAX MXSB_LD_E_MANIFEST_SYNTAX
#define MXSB_E_MANIFEST_VERSION MXSB_LD_E_MANIFEST_VERSION
#define MXSB_E_BUILD_ID MXSB_LD_E_BUILD_ID
#define MXSB_E_CAPS_SUMMARY MXSB_LD_E_CAPS_SUMMARY
#define MXSB_E_PADDING MXSB_LD_E_PADDING
#define MXSB_E_LAYOUT MXSB_LD_E_LAYOUT
#define MXSB_S_MANIFEST MXSB_LD_S_MANIFEST
#define MXSB_S_TEXT MXSB_LD_S_TEXT
#define MXSB_S_DATA MXSB_LD_S_DATA
#define MXSB_S_SIGNATURE MXSB_LD_S_SIGNATURE
#define MXSB_S_BUILD_INFO MXSB_LD_S_BUILD_INFO
#define MXSB_SF_R MXSB_LD_SF_R
#define MXSB_SF_W MXSB_LD_SF_W
#define MXSB_SF_X MXSB_LD_SF_X
#define mxsb_err_name mxsb_ld_err_name
typedef struct { const char *rule; char detail[200]; } mxsb_diag;
static void mx_sha256_oneshot(const uint8_t *p, size_t n, uint8_t out[32]) { mxsb_ld_sha256(p, n, out); }
static void mx_sha256_hex(const uint8_t h[32], char out[65]) { for (int i = 0; i < 32; ++i) snprintf(out + 2 * i, 3, "%02x", h[i]); }
static mxsb_err mxsb_validate(const uint8_t *b, size_t len, mxsb_diag *d) {
    mxsb_ld_policy pol; memset(&pol, 0, sizeof pol); pol.skip_signature = true; /* structural rules only, V20/V26 are the host's */
    static mxsb_ld_image im; mxsb_ld_diag ld; memset(&ld, 0, sizeof ld);
    mxsb_err rc = mxsb_ld_check(b, len, &pol, &im, &ld);
    d->rule = ld.rule; memcpy(d->detail, ld.detail, sizeof d->detail); return rc;
}
#define BACKEND "mxsb-ref"
#else
#include "mxsb.h"
#include "mx_sha256.h"
#define BACKEND "codegen/mxsb"
#endif

static int g_pass = 0, g_fail = 0;
static void wr16(uint8_t *p, uint16_t v) { for (int i = 0; i < 2; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void wr64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t rd64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }

/* layout: header(64) | table(3x32 = 96) @64 | MANIFEST @160 (padded to 16) | TEXT (304 = 19x16) | SIGNATURE (96) */
#define TEXT_SZ 304u
#define SIG_SZ 96u
static size_t g_len; static uint64_t off_man, off_text, off_sig, man_sz;
static int ent_man = 0, ent_text = 1, ent_sig = 2;

static uint8_t *build_valid(void) {
    uint8_t text[TEXT_SZ]; for (unsigned i = 0; i < TEXT_SZ; ++i) text[i] = (uint8_t)(i * 13 + 5);
    uint8_t h[32]; mx_sha256_oneshot(text, TEXT_SZ, h); char hex[65]; mx_sha256_hex(h, hex);
    char man[256]; int mlen = snprintf(man, sizeof man, "mxsb-manifest 1\nname harness.app\nabi native\nbuild_id %s\n", hex);
    man_sz = (uint64_t)mlen; uint64_t man_pad = (man_sz + 15) & ~15ull;
    off_man = 64 + 96; off_text = off_man + man_pad; off_sig = off_text + TEXT_SZ; g_len = (size_t)(off_sig + SIG_SZ);
    uint8_t *b = calloc(1, g_len + 128);
    memcpy(b, "MXSB", 4); b[4] = 2; b[5] = 0; wr16(b + 6, 64); wr32(b + 8, 0 /* flags */); wr32(b + 12, 3 /* section_count */);
    wr64(b + 16, 64 /* section_table_off */); wr64(b + 24, off_text /* entry_off */); wr64(b + 32, 0 /* caps_summary */); wr64(b + 40, g_len);
    /* table entries: type u32, flags u32, offset u64, size u64, reserved u64; sorted by offset */
    uint8_t *e = b + 64;
    wr32(e + 0, MXSB_S_MANIFEST); wr32(e + 4, 0); wr64(e + 8, off_man); wr64(e + 16, man_sz); wr64(e + 24, 0);
    e += 32; wr32(e + 0, MXSB_S_TEXT); wr32(e + 4, MXSB_SF_R | MXSB_SF_X); wr64(e + 8, off_text); wr64(e + 16, TEXT_SZ); wr64(e + 24, 0);
    e += 32; wr32(e + 0, MXSB_S_SIGNATURE); wr32(e + 4, 0); wr64(e + 8, off_sig); wr64(e + 16, SIG_SZ); wr64(e + 24, 0);
    memcpy(b + off_man, man, (size_t)man_sz); memcpy(b + off_text, text, TEXT_SZ); /* signature: 96 zero bytes (V20 is the caller's) */
    return b;
}

static void expect(const char *name, const uint8_t *b, size_t len, mxsb_err want, const char *rule) {
    mxsb_diag d; memset(&d, 0, sizeof d); mxsb_err got = mxsb_validate(b, len, &d);
    int ok = got == want && (rule == NULL || (d.rule && strcmp(d.rule, rule) == 0) || want == MXSB_OK);
    if (ok) g_pass++; else { g_fail++; printf("FAIL %-48s want %s/%s got %s/%s (%s)\n", name, mxsb_err_name(want), rule ? rule : "-", mxsb_err_name(got), d.rule ? d.rule : "-", d.detail); }
}
#define E(i, f) (b + 64 + 32 * (i) + (f))   /* table entry i, field offset f: 0 type, 4 flags, 8 offset, 16 size, 24 reserved */

int main(void) {
    uint8_t *base = build_valid(); uint8_t *b = malloc(g_len + 128); size_t len = g_len;
#define R() memcpy(b, base, g_len + 128)
    R(); expect("hand-built minimal file validates", b, len, MXSB_OK, NULL);
    R(); expect("V1 len 63", b, 63, MXSB_E_HEADER, "V1");
    R(); b[1] = 'x'; expect("V2 magic", b, len, MXSB_E_MAGIC, "V2");
    R(); b[4] = 1; expect("V3 v1 refused by the loader", b, len, MXSB_E_VERSION, "V3");
    R(); b[4] = 3; expect("V3 unknown major", b, len, MXSB_E_VERSION, "V3");
    R(); b[5] = 7; expect("minor 7 accepted (additive)", b, len, MXSB_OK, NULL);
    R(); wr16(b + 6, 48); expect("V4 header_size 48", b, len, MXSB_E_HEADER, "V4");
    R(); wr16(b + 6, 4097); expect("V4 header_size 4097", b, len, MXSB_E_HEADER, "V4");
    R(); wr16(b + 6, 4096); expect("V4 header_size > len", b, len, MXSB_E_HEADER, "V4");
    R(); wr64(b + 40, len + 1); expect("V5 file_size != len", b, len, MXSB_E_HEADER, "V5");
    R(); wr32(b + 8, 8); expect("V6 flags bit 3", b, len, MXSB_E_HEADER, "V6");
    R(); b[48 + 15] = 1; expect("V6 reserved byte nonzero", b, len, MXSB_E_HEADER, "V6");
    R(); wr32(b + 12, 0); expect("V7 section_count 0", b, len, MXSB_E_TABLE, "V7");
    R(); wr32(b + 12, 65); expect("V7 section_count 65", b, len, MXSB_E_TABLE, "V7");
    R(); wr64(b + 16, 72); expect("V8 table_off not 16-aligned", b, len, MXSB_E_TABLE, "V8");
    R(); wr64(b + 16, 48); expect("V8 table_off below header_size", b, len, MXSB_E_TABLE, "V8");
    R(); wr64(b + 16, (uint64_t)len - 16); expect("V9 table past end", b, len, MXSB_E_TABLE, "V9");
    R(); wr64(b + 16, UINT64_MAX - 15); expect("V9 table_off + bytes overflows", b, len, MXSB_E_TABLE, "V9");
    R(); wr64(E(ent_text, 24), 1); expect("V10 entry reserved nonzero", b, len, MXSB_E_TABLE, "V10");
    R(); wr32(E(ent_text, 4), MXSB_SF_R | MXSB_SF_W); expect("V10 TEXT flags R+W", b, len, MXSB_E_TABLE, "V10");
    R(); wr32(E(ent_man, 4), MXSB_SF_R); expect("V10 flags on MANIFEST", b, len, MXSB_E_TABLE, "V10");
    R(); wr64(E(ent_text, 8), off_text + 8); expect("V11 offset not 16-aligned", b, len, MXSB_E_BOUNDS, "V11");
    R(); wr64(E(ent_text, 16), 0); expect("V11 size 0", b, len, MXSB_E_BOUNDS, "V11");
    R(); wr64(E(ent_sig, 16), UINT64_MAX); expect("V11 offset + size overflows u64", b, len, MXSB_E_BOUNDS, "V11");
    R(); wr64(E(ent_sig, 16), SIG_SZ + 1); expect("V11 section past end of file by one byte", b, len, MXSB_E_BOUNDS, "V11");
    R(); wr64(E(ent_man, 8), 64); expect("V11 section inside the table", b, len, MXSB_E_BOUNDS, "V11");
    R(); wr64(E(ent_sig, 8), off_text); expect("V13 overlap / order", b, len, MXSB_E_OVERLAP, "V13");
    R(); wr64(E(ent_sig, 8), off_sig - 16); expect("V13 one-paragraph overlap", b, len, MXSB_E_OVERLAP, "V13");
    R(); wr32(E(ent_sig, 0), MXSB_S_MANIFEST); expect("V14 duplicate type", b, len, MXSB_E_DUP_SECTION, "V14");
    R(); wr32(E(ent_sig, 0), MXSB_S_BUILD_INFO); expect("V15 SIGNATURE missing", b, len, MXSB_E_MISSING_SECTION, "V15");
    R(); wr64(E(ent_sig, 16), SIG_SZ - 1); expect("V16 SIGNATURE.size 95", b, len, MXSB_E_TABLE, "V16");
    R(); wr64(b + 24, off_text + TEXT_SZ); expect("V17 entry_off == TEXT end", b, len, MXSB_E_ENTRY, "V17");
    R(); wr64(b + 24, off_text - 1); expect("V17 entry_off before TEXT", b, len, MXSB_E_ENTRY, "V17");
    R(); b[off_man + 5] = 0; expect("V21 NUL in manifest", b, len, MXSB_E_MANIFEST_SYNTAX, "V21");
    R(); b[off_man + 5] = 0x01; expect("V21 control char in manifest", b, len, MXSB_E_MANIFEST_SYNTAX, "V21");
    R(); b[off_man + 14] = '2'; expect("V22 mxsb-manifest 2", b, len, MXSB_E_MANIFEST_VERSION, "V22");
    R(); memcpy(b + off_man + 33, "nativx", 6); expect("V22 unknown abi", b, len, MXSB_E_MANIFEST_SYNTAX, "V22");
    R(); { uint8_t *p = b + off_man + 53; *p = (*p == '0') ? '1' : '0'; } expect("V23 build_id digit flipped", b, len, MXSB_E_BUILD_ID, "V23");
    R(); b[off_text + 100] ^= 1; expect("V23 TEXT byte changed -> build_id mismatch", b, len, MXSB_E_BUILD_ID, "V23");
    R(); wr64(b + 32, 1); expect("V24 caps_summary claims fs.read with no require", b, len, MXSB_E_CAPS_SUMMARY, "V24");
    R(); wr64(b + 32, 1ull << 10); expect("V24 reserved caps bit", b, len, MXSB_E_CAPS_SUMMARY, "V24");
    R(); b[off_man + man_sz] = 0xFF; expect("V27 nonzero padding after MANIFEST", b, len, MXSB_E_PADDING, "V27");
    /* a non-minimal case: TEXT not page-sized with DATA present must fail V28 -- needs a 4-entry table, built by shifting the signature */
    { R(); size_t L2 = g_len + 32 + 16; uint8_t *c = calloc(1, L2 + 128); memcpy(c, base, 64); wr32(c + 12, 4); wr64(c + 40, L2);
      uint64_t o_man = 64 + 128, o_text = o_man + ((man_sz + 15) & ~15ull), o_data = o_text + TEXT_SZ, o_sig = o_data + 16;
      wr64(c + 24, o_text);
      uint8_t *e = c + 64; wr32(e, MXSB_S_MANIFEST); wr64(e + 8, o_man); wr64(e + 16, man_sz);
      e += 32; wr32(e, MXSB_S_TEXT); wr32(e + 4, MXSB_SF_R | MXSB_SF_X); wr64(e + 8, o_text); wr64(e + 16, TEXT_SZ);
      e += 32; wr32(e, MXSB_S_DATA); wr32(e + 4, MXSB_SF_R | MXSB_SF_W); wr64(e + 8, o_data); wr64(e + 16, 16);
      e += 32; wr32(e, MXSB_S_SIGNATURE); wr64(e + 8, o_sig); wr64(e + 16, SIG_SZ);
      memcpy(c + o_man, base + off_man, (size_t)man_sz); memcpy(c + o_text, base + off_text, TEXT_SZ); memset(c + o_data, 0x11, 16);
      expect("V28 DATA present but TEXT.size not a page multiple", c, L2, MXSB_E_LAYOUT, "V28"); free(c); }
    (void)rd64;
    printf("mxsb_contract[%s]: %d passed, %d failed\n", BACKEND, g_pass, g_fail); free(b); free(base); return g_fail ? 1 : 0;
}
