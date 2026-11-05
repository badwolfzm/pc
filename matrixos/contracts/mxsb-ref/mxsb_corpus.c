/*
 * mxsb_corpus -- builds the reference corpus: one valid MXSB v2 file (built
 * by an independent minimal emitter, unsigned) and one mutant per
 * validation rule, each expected to fail with a named error.
 *
 *   mxsb_corpus OUTDIR
 *
 * Writes OUTDIR/<name>.mxsb and OUTDIR/expected.txt with lines
 * "<name> <expected error> <rule>". run_tests.sh feeds every file to
 * mxsb_check (and to the codegen emitter's verifier) and compares.
 */
#define _GNU_SOURCE
#include "mxsb_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void w16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8*i)); }
static void w64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8*i)); }
static uint64_t r16(uint64_t x) { return (x + 15) & ~15ull; }

typedef struct { uint32_t type, flags; uint64_t size; const uint8_t *data; } sec;

/* Lay out: header(64) | table | sections in given order, 16-aligned, zero padding. */
static uint8_t *build(const sec *s, uint32_t n, uint32_t flags, uint64_t entry_rel_text, uint64_t caps, size_t *outlen) {
    uint64_t table_off = 64, cursor = r16(table_off + (uint64_t)n * 32);
    uint64_t off[64]; uint64_t text_off = 0;
    for (uint32_t i = 0; i < n; ++i) { if (s[i].type == MXSB_LD_S_BSS) { off[i] = 0; continue; } off[i] = cursor; cursor = r16(cursor + s[i].size); if (s[i].type == MXSB_LD_S_TEXT) text_off = cursor - r16(s[i].size); }
    /* recompute text_off precisely */
    for (uint32_t i = 0; i < n; ++i) if (s[i].type == MXSB_LD_S_TEXT) text_off = off[i];
    uint64_t total = cursor;
    uint8_t *b = calloc(1, total);
    memcpy(b, "MXSB", 4); b[4] = 2; b[5] = 0; w16(b + 6, 64); w32(b + 8, flags); w32(b + 12, n);
    w64(b + 16, table_off); w64(b + 24, text_off + entry_rel_text); w64(b + 32, caps); w64(b + 40, total);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t *e = b + table_off + (uint64_t)i * 32;
        w32(e, s[i].type); w32(e + 4, s[i].flags); w64(e + 8, off[i]); w64(e + 16, s[i].size); w64(e + 24, 0);
        if (s[i].type != MXSB_LD_S_BSS && s[i].data) memcpy(b + off[i], s[i].data, s[i].size);
    }
    *outlen = total; return b;
}
static void hex32(const uint8_t d[32], char out[65]) { for (int k = 0; k < 32; ++k) snprintf(out + 2*k, 3, "%02x", d[k]); }

static FILE *g_exp;
static const char *g_dir;
static void emit(const char *name, const uint8_t *b, size_t n, const char *err, const char *rule) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.mxsb", g_dir, name);
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); exit(2); }
    fwrite(b, 1, n, f); fclose(f);
    fprintf(g_exp, "%s %s %s\n", name, err, rule);
}
/* find the table entry offset for a type */
static uint64_t entry_of(const uint8_t *b, uint32_t type) {
    uint32_t n; memcpy(&n, b + 12, 4);
    for (uint32_t i = 0; i < n; ++i) { uint32_t t; memcpy(&t, b + 64 + i*32, 4); if (t == type) return 64 + (uint64_t)i * 32; }
    return 0;
}
static uint64_t rd64(const uint8_t *b, uint64_t off) { uint64_t v; memcpy(&v, b + off, 8); return v; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: mxsb_corpus OUTDIR\n"); return 2; }
    g_dir = argv[1];
    char ep[1024]; snprintf(ep, sizeof ep, "%s/expected.txt", g_dir);
    g_exp = fopen(ep, "w"); if (!g_exp) { perror(ep); return 2; }

    /* --- the valid file: TEXT (one page), DATA, BSS, MANIFEST, SUPEROPS, BUILD_INFO, SIGNATURE (zero = unsigned) --- */
    static uint8_t text[4096]; for (size_t i = 0; i < sizeof text; ++i) text[i] = (uint8_t)(0xC3 ^ (i * 7));   /* ret-ish filler */
    static uint8_t data[200]; for (size_t i = 0; i < sizeof data; ++i) data[i] = (uint8_t)(i * 3);
    uint8_t bid[32]; char bidhex[65];
    { mxsb_ld_sha256_ctx c; mxsb_ld_sha256_init(&c); mxsb_ld_sha256_update(&c, text, sizeof text); mxsb_ld_sha256_update(&c, data, sizeof data); mxsb_ld_sha256_final(&c, bid); hex32(bid, bidhex); }
    char manifest[2048];
    int ml = snprintf(manifest, sizeof manifest,
        "# reference corpus manifest\nmxsb-manifest 1\nname  ref-demo\nabi native\nbuild_id %s\n"
        "require fs.read=/app\nrequire   fs.read=/app/assets\nrequire fs.write=/data/out\nrequire net.connect=db.internal:5432\n"
        "require render\nrequire superop=blit\nrequire superop=clear\nrequire clock=coarse\nrequire random\nrequire memory.max=256M\nrequire cpu.max_cores=2\n"
        "require fs.read=/app\n", bidhex);   /* duplicate line: must normalise, not refuse */
    uint8_t superops[8]; w32(superops, 0x13); w32(superops + 4, 0x11);
    const char *binfo = "profile=matrixos\nsimd_tier=baseline\n";
    uint8_t sig[96] = {0};
    uint64_t caps = (1ull<<0)|(1ull<<1)|(1ull<<2)|(1ull<<3)|(1ull<<4)|(1ull<<5)|(1ull<<6);
    sec S[] = {
        { MXSB_LD_S_MANIFEST, 0, (uint64_t)ml, (const uint8_t *)manifest },
        { MXSB_LD_S_TEXT, 5, sizeof text, text },
        { MXSB_LD_S_DATA, 3, sizeof data, data },
        { MXSB_LD_S_BSS, 3, 8192, NULL },
        { MXSB_LD_S_SUPEROPS, 0, sizeof superops, superops },
        { MXSB_LD_S_BUILD_INFO, 0, strlen(binfo), (const uint8_t *)binfo },
        { MXSB_LD_S_SIGNATURE, 0, 96, sig },
    };
    size_t n; uint8_t *v = build(S, 7, 1 /*PIE*/, 16, caps, &n);
    emit("valid_unsigned", v, n, "MXSB_OK", "-");
    /* a minimal valid file: MANIFEST, TEXT, SIGNATURE only, no DATA, odd TEXT size allowed */
    {
        uint8_t t2[100]; memset(t2, 0x90, sizeof t2);
        uint8_t b2[32]; char h2[65]; mxsb_ld_sha256(t2, sizeof t2, b2); hex32(b2, h2);
        char m2[256]; int l2 = snprintf(m2, sizeof m2, "mxsb-manifest 1\nname min\nabi native\nbuild_id %s\n", h2);
        sec S2[] = { { MXSB_LD_S_MANIFEST, 0, (uint64_t)l2, (const uint8_t *)m2 }, { MXSB_LD_S_TEXT, 5, sizeof t2, t2 }, { MXSB_LD_S_SIGNATURE, 0, 96, sig } };
        size_t n2; uint8_t *v2 = build(S2, 3, 0, 0, 0, &n2);
        emit("valid_minimal", v2, n2, "MXSB_OK", "-");
        /* V28 counter-case: odd TEXT with DATA present */
        uint8_t d2[16] = {1}; uint8_t b3[32]; char h3[65];
        { mxsb_ld_sha256_ctx c; mxsb_ld_sha256_init(&c); mxsb_ld_sha256_update(&c, t2, sizeof t2); mxsb_ld_sha256_update(&c, d2, 16); mxsb_ld_sha256_final(&c, b3); hex32(b3, h3); }
        char m3[256]; int l3 = snprintf(m3, sizeof m3, "mxsb-manifest 1\nname min\nabi native\nbuild_id %s\n", h3);
        sec S3[] = { { MXSB_LD_S_MANIFEST, 0, (uint64_t)l3, (const uint8_t *)m3 }, { MXSB_LD_S_TEXT, 5, sizeof t2, t2 }, { MXSB_LD_S_DATA, 3, 16, d2 }, { MXSB_LD_S_SIGNATURE, 0, 96, sig } };
        size_t n3; uint8_t *v3 = build(S3, 4, 0, 0, 0, &n3);
        emit("v28_text_not_page_multiple", v3, n3, "MXSB_E_LAYOUT", "V28");
        free(v2); free(v3);
    }
    uint8_t *m = malloc(n + 4096);
#define MUT(name, err, rule, body) do { memcpy(m, v, n); size_t mn = n; body; emit(name, m, mn, err, rule); } while (0)
    MUT("v1_short", "MXSB_E_HEADER", "V1", { mn = 63; });
    MUT("v2_magic", "MXSB_E_MAGIC", "V2", { m[0] = 'X'; });
    MUT("v3_major1_v1file", "MXSB_E_VERSION", "V3", { m[4] = 1; });
    MUT("v3_major3", "MXSB_E_VERSION", "V3", { m[4] = 3; });
    MUT("v4_header_size_63", "MXSB_E_HEADER", "V4", { w16(m + 6, 63); });
    MUT("v4_header_size_huge", "MXSB_E_HEADER", "V4", { w16(m + 6, 8192); });
    MUT("v5_file_size_off_by_one", "MXSB_E_HEADER", "V5", { w64(m + 40, n + 1); });
    MUT("v6_reserved_flag", "MXSB_E_HEADER", "V6", { w32(m + 8, 1u << 31); });
    MUT("v6_reserved_bytes", "MXSB_E_HEADER", "V6", { m[50] = 1; });
    MUT("v7_count_zero", "MXSB_E_TABLE", "V7", { w32(m + 12, 0); });
    MUT("v7_count_65", "MXSB_E_TABLE", "V7", { w32(m + 12, 65); });
    MUT("v8_table_unaligned", "MXSB_E_TABLE", "V8", { w64(m + 16, 72); });
    MUT("v9_table_past_end", "MXSB_E_TABLE", "V9", { w64(m + 16, r16(n)); });
    MUT("v9_table_overflow", "MXSB_E_TABLE", "V9", { w64(m + 16, UINT64_MAX - 15); });
    MUT("v10_entry_reserved", "MXSB_E_TABLE", "V10", { uint64_t e = entry_of(m, MXSB_LD_S_TEXT); m[e + 24] = 1; });
    MUT("v10_text_flags_rwx", "MXSB_E_TABLE", "V10", { uint64_t e = entry_of(m, MXSB_LD_S_TEXT); w32(m + e + 4, 7); });
    MUT("v10_manifest_flags", "MXSB_E_TABLE", "V10", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); w32(m + e + 4, 1); });
    MUT("v10_future_type_skipped", "MXSB_OK", "-", { uint64_t e = entry_of(m, MXSB_LD_S_BUILD_INFO); w32(m + e, 9); /* a type from a later minor: skipped, still digested */ });
    MUT("v10_type_zero", "MXSB_E_TABLE", "V10", { uint64_t e = entry_of(m, MXSB_LD_S_BUILD_INFO); w32(m + e, 0); });
    MUT("v11_offset_unaligned", "MXSB_E_BOUNDS", "V11", { uint64_t e = entry_of(m, MXSB_LD_S_BUILD_INFO); w64(m + e + 8, rd64(m, e + 8) + 1); });
    MUT("v11_offset_in_table", "MXSB_E_BOUNDS", "V11", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); w64(m + e + 8, 64); });
    MUT("v11_size_zero", "MXSB_E_BOUNDS", "V11", { uint64_t e = entry_of(m, MXSB_LD_S_BUILD_INFO); w64(m + e + 16, 0); });
    MUT("v11_size_past_end", "MXSB_E_BOUNDS", "V11", { uint64_t e = entry_of(m, MXSB_LD_S_SIGNATURE); w64(m + e + 16, n); });
    MUT("v11_size_u64_max", "MXSB_E_BOUNDS", "V11", { uint64_t e = entry_of(m, MXSB_LD_S_SIGNATURE); w64(m + e + 16, UINT64_MAX); });
    MUT("v11_offset_plus_size_overflow", "MXSB_E_BOUNDS", "V11", { uint64_t e = entry_of(m, MXSB_LD_S_SIGNATURE); w64(m + e + 8, UINT64_MAX - 95); });
    MUT("v12_bss_offset", "MXSB_E_BOUNDS", "V12", { uint64_t e = entry_of(m, MXSB_LD_S_BSS); w64(m + e + 8, 64); });
    MUT("v12_bss_too_big", "MXSB_E_BOUNDS", "V12", { uint64_t e = entry_of(m, MXSB_LD_S_BSS); w64(m + e + 16, (1ull << 30) + 1); });
    MUT("v13_overlap", "MXSB_E_OVERLAP", "V13", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); w64(m + e + 16, rd64(m, e + 16) + 4096); /* now runs into TEXT */ });
    MUT("v13_unsorted", "MXSB_E_OVERLAP", "V13", { uint64_t a = entry_of(m, MXSB_LD_S_MANIFEST); uint64_t b = entry_of(m, MXSB_LD_S_TEXT); uint8_t tmp[32]; memcpy(tmp, m + a, 32); memcpy(m + a, m + b, 32); memcpy(m + b, tmp, 32); });
    MUT("v27_padding_byte", "MXSB_E_PADDING", "V27", { uint64_t e = entry_of(m, MXSB_LD_S_BUILD_INFO); m[rd64(m, e + 8) + rd64(m, e + 16)] = 0xFF; /* first pad byte after BUILD_INFO (35 bytes, so padding exists) */ });
    MUT("v27_trailing_bytes", "MXSB_E_PADDING", "V27", { m[mn] = 0x41; mn += 16; w64(m + 40, mn); });
    MUT("v27_header_extension", "MXSB_E_PADDING", "V27", {
        /* grow header to 80: shift everything after 64 by 16, fix table_off and offsets, put a non-zero byte in 64..80 */
        memmove(m + 80, m + 64, n - 64); memset(m + 64, 0, 16); mn = n + 16; w16(m + 6, 80); w64(m + 16, 80); w64(m + 40, mn);
        uint32_t cnt; memcpy(&cnt, m + 12, 4);
        for (uint32_t i = 0; i < cnt; ++i) { uint64_t e = 80 + (uint64_t)i * 32; uint32_t ty; memcpy(&ty, m + e, 4); if (ty != MXSB_LD_S_BSS) w64(m + e + 8, rd64(m, e + 8) + 16); }
        w64(m + 24, rd64(m, 24) + 16);
        m[70] = 1; });
    MUT("v14_dup_section", "MXSB_E_DUP_SECTION", "V14", { uint64_t e = entry_of(m, MXSB_LD_S_BUILD_INFO); w32(m + e, MXSB_LD_S_SUPEROPS); w32(m + e + 4, 0); });
    MUT("v15_no_signature", "MXSB_E_MISSING_SECTION", "V15", { uint64_t e = entry_of(m, MXSB_LD_S_SIGNATURE); w32(m + e, 0x8000); });
    MUT("v15_no_text", "MXSB_E_MISSING_SECTION", "V15", { uint64_t e = entry_of(m, MXSB_LD_S_TEXT); w32(m + e, 0x8001); w32(m + e + 4, 0); });
    MUT("v16_signature_size", "MXSB_E_TABLE", "V16", { uint64_t e = entry_of(m, MXSB_LD_S_SIGNATURE); w64(m + e + 16, 95); });
    MUT("v16_superops_size", "MXSB_E_TABLE", "V16", { uint64_t e = entry_of(m, MXSB_LD_S_SUPEROPS); w64(m + e + 16, 7); });
    MUT("v17_entry_before_text", "MXSB_E_ENTRY", "V17", { w64(m + 24, 64); });
    MUT("v17_entry_at_text_end", "MXSB_E_ENTRY", "V17", { uint64_t e = entry_of(m, MXSB_LD_S_TEXT); w64(m + 24, rd64(m, e + 8) + rd64(m, e + 16)); });
    MUT("v18_bss_1gib_ok", "MXSB_OK", "-", { uint64_t e = entry_of(m, MXSB_LD_S_BSS); w64(m + e + 16, 1ull << 30); /* largest BSS; V18 total stays under 4 GiB (a > 4 GiB case cannot fit the 1 GiB BSS cap plus an in-file TEXT/DATA we can write here) */ });
    MUT("v20_unsigned_not_allowed", "MXSB_E_SIGNATURE", "V20", { (void)0; });   /* valid file, but checked without allow_unsigned */
    MUT("v20_unsigned_not_approved", "MXSB_E_UNTRUSTED_KEY", "V20", { (void)0; }); /* valid file, allow_unsigned but digest not on the list */
    MUT("v20_garbage_signature", "MXSB_E_UNTRUSTED_KEY", "V20", { uint64_t e = entry_of(m, MXSB_LD_S_SIGNATURE); m[rd64(m, e + 8)] = 0x7F; });
    MUT("v21_control_char", "MXSB_E_MANIFEST_SYNTAX", "V21", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); m[rd64(m, e + 8) + 2] = 0x01; });
    MUT("v21_nul", "MXSB_E_MANIFEST_SYNTAX", "V21", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); m[rd64(m, e + 8) + 2] = 0; });
    MUT("v21_bad_utf8", "MXSB_E_MANIFEST_SYNTAX", "V21", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); m[rd64(m, e + 8) + 2] = 0xC3; m[rd64(m, e + 8) + 3] = 0x41; });
    MUT("v22_unknown_perm", "MXSB_E_UNKNOWN_PERM", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), "render", 6); memcpy(q, "rendex", 6); });
    MUT("v22_relative_path", "MXSB_E_BAD_SCOPE", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), "=/data", 6); memcpy(q, "=xdata", 6); });
    MUT("v22_no_port", "MXSB_E_BAD_SCOPE", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), ":5432", 5); memcpy(q, "x5432", 5); });
    MUT("v22_clock_fast", "MXSB_E_BAD_SCOPE", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), "coarse", 6); memcpy(q, "fast  ", 6); });
    MUT("v22_schema_version", "MXSB_E_MANIFEST_VERSION", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), "manifest 1", 10); q[9] = '2'; });
    MUT("v22_missing_first_line", "MXSB_E_MANIFEST_SYNTAX", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), "mxsb-manifest", 13); q[0] = 'y'; });
    MUT("v22_dotdot_path", "MXSB_E_BAD_SCOPE", "V22", { uint64_t e = entry_of(m, MXSB_LD_S_MANIFEST); uint8_t *p = m + rd64(m, e + 8); char *q = memmem(p, (size_t)rd64(m, e + 16), "/app/assets", 11); memcpy(q, "/app/../etc", 11); });
    MUT("v23_text_bit_flip", "MXSB_E_BUILD_ID", "V23", { uint64_t e = entry_of(m, MXSB_LD_S_TEXT); m[rd64(m, e + 8) + 100] ^= 1; });
    MUT("v23_data_bit_flip", "MXSB_E_BUILD_ID", "V23", { uint64_t e = entry_of(m, MXSB_LD_S_DATA); m[rd64(m, e + 8) + 10] ^= 1; });
    MUT("v24_caps_summary_low", "MXSB_E_CAPS_SUMMARY", "V24", { w64(m + 32, caps & ~1ull); });
    MUT("v24_caps_summary_extra", "MXSB_E_CAPS_SUMMARY", "V24", { w64(m + 32, caps | (1ull << 8)); });
    MUT("v24_caps_summary_reserved", "MXSB_E_CAPS_SUMMARY", "V24", { w64(m + 32, caps | (1ull << 40)); });
    MUT("v25_superop_not_required", "MXSB_E_NOT_GRANTED", "V25", { uint64_t e = entry_of(m, MXSB_LD_S_SUPEROPS); w32(m + rd64(m, e + 8), 0x21); /* present, not in manifest */ });
    MUT("v25_superop_unknown_id", "MXSB_E_NOT_GRANTED", "V25", { uint64_t e = entry_of(m, MXSB_LD_S_SUPEROPS); w32(m + rd64(m, e + 8), 0xFFFF); });
    /* V26 cases use the valid file against restricted containers; the runner picks the container by name */
    MUT("v26_fs_read_outside_grant", "MXSB_E_NOT_GRANTED", "V26", { (void)0; });
    MUT("v26_no_render", "MXSB_E_NOT_GRANTED", "V26", { (void)0; });
    MUT("v26_random_not_explicit", "MXSB_E_NOT_GRANTED", "V26", { (void)0; });
    MUT("v26_memory_over_budget", "MXSB_E_NOT_GRANTED", "V26", { (void)0; });
    MUT("v26_superop_not_listed", "MXSB_E_NOT_GRANTED", "V26", { (void)0; });
    MUT("v26_clock_too_precise", "MXSB_E_NOT_GRANTED", "V26", { (void)0; });
    fclose(g_exp); free(m); free(v);
    return 0;
}
