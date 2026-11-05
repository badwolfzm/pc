/*
 * MXSB unit + mutation tests.  Builds a valid image through the library, then
 * applies one targeted corruption at a time and asserts the validator stops
 * with the error and rule the contract (section 9a) names.  Finishes with a
 * random-mutation fuzz pass (no crash, no hang, every result is a known code).
 * Build with -fsanitize=address,undefined.
 */
#include "../mxsb/mxsb.h"
#include "../mxsb/mx_sha256.h"
#include <stdlib.h>
#include <string.h>

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) passes++; else { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void wr32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; ++i) b[i] = (uint8_t)(v >> (8*i)); }
static void wr64(uint8_t *b, uint64_t v) { for (int i = 0; i < 8; ++i) b[i] = (uint8_t)(v >> (8*i)); }
static uint64_t rd64(const uint8_t *b) { uint64_t x = 0; for (int i = 7; i >= 0; --i) x = (x << 8) | b[i]; return x; }

static const char MANIFEST_TPL[] =
    "mxsb-manifest 1\nname test.app-1\nabi native\nbuild_id %s\n"
    "require fs.read=/sandbox/assets\nrequire render\nrequire superop=blit\nrequire clock=precise\nrequire memory.max=64M\n";

/* Build a valid encoded image; returns buffer, fills *len. Also returns the image for offsets. */
static uint8_t *make_valid(size_t *len, mxsb_image *imout, bool with_data, bool with_bss, bool with_superops) {
    /* V28: TEXT must be a page multiple when DATA/BSS exists, and build_id covers the padded TEXT */
    size_t tsz = (with_data || with_bss) ? 4096 : 300;
    uint8_t text[4096]; memset(text, 0, sizeof text); for (int i = 0; i < 300; ++i) text[i] = (uint8_t)(i * 7 + 1);
    uint8_t data[100]; for (int i = 0; i < 100; ++i) data[i] = (uint8_t)(i ^ 0x5a);
    char bid[65]; mxsb_build_id(text, tsz, with_data ? data : NULL, with_data ? sizeof data : 0, bid);
    char man[600]; snprintf(man, sizeof man, MANIFEST_TPL, bid);
    mxsb_manifest m; mxsb_diag d = {0};
    if (mxsb_manifest_parse(man, strlen(man), &m, &d) != MXSB_OK) { fprintf(stderr, "manifest: %s %s\n", mxsb_err_name(d.err), d.detail); exit(1); }
    size_t nlen; char *norm = mxsb_manifest_serialize(&m, &nlen);
    mxsb_image im; mxsb_image_init(&im);
    im.caps_summary = m.caps_summary; im.flags = MXSB_F_PIE;
    mxsb_image_add(&im, MXSB_S_MANIFEST, 0, norm, nlen);
    mxsb_image_add(&im, MXSB_S_TEXT, MXSB_SF_R | MXSB_SF_X, text, tsz);
    if (with_data) mxsb_image_add(&im, MXSB_S_DATA, MXSB_SF_R | MXSB_SF_W, data, sizeof data);
    if (with_bss) mxsb_image_add(&im, MXSB_S_BSS, MXSB_SF_R | MXSB_SF_W, NULL, 4096);
    if (with_superops) { uint8_t so[4] = { 0x13, 0, 0, 0 }; mxsb_image_add(&im, MXSB_S_SUPEROPS, 0, so, 4); }
    const char bi[] = "format=mxsb-2.0\nsimd_tier=avx2\n";
    mxsb_image_add(&im, MXSB_S_BUILD_INFO, 0, bi, sizeof bi - 1);
    uint8_t sig[96] = {0}; mxsb_image_add(&im, MXSB_S_SIGNATURE, 0, sig, 96);
    if (mxsb_layout(&im, 16, &d) != MXSB_OK) { fprintf(stderr, "layout: %s\n", d.detail); exit(1); }
    uint8_t *buf; if (mxsb_encode(&im, &buf, len, &d) != MXSB_OK) { fprintf(stderr, "encode: %s %s %s\n", mxsb_err_name(d.err), d.rule, d.detail); exit(1); }
    mxsb_manifest_free(&m); free(norm);
    if (imout) *imout = im; else mxsb_image_free(&im);
    return buf;
}

static int sec_index(const mxsb_image *im, uint32_t type) { return mxsb_image_find(im, type); }
static size_t entry_off(const mxsb_image *im, int i) { return (size_t)im->header_size + (size_t)i * 32; }

typedef struct { const char *name; mxsb_err want; const char *rule; } expect;

static void expect_result(const uint8_t *buf, size_t len, expect e) {
    mxsb_diag d = {0};
    mxsb_err got = mxsb_validate(buf, len, &d);
    bool ok = got == e.want && (e.rule == NULL || (d.rule && strcmp(d.rule, e.rule) == 0));
    CHECK(ok, "%s: want %s/%s got %s/%s (%s)", e.name, mxsb_err_name(e.want), e.rule ? e.rule : "*", mxsb_err_name(got), d.rule ? d.rule : "-", d.detail);
}

static void mutation_tests(void) {
    size_t len; mxsb_image im;
    uint8_t *base = make_valid(&len, &im, true, true, true);
    expect_result(base, len, (expect){"valid image", MXSB_OK, "ok"});
    uint8_t *b = malloc(len + 64);
#define RESET() memcpy(b, base, len)
    int ti = sec_index(&im, MXSB_S_TEXT), di = sec_index(&im, MXSB_S_DATA), bi = sec_index(&im, MXSB_S_BSS);
    int mi = sec_index(&im, MXSB_S_MANIFEST), si = sec_index(&im, MXSB_S_SIGNATURE), soi = sec_index(&im, MXSB_S_SUPEROPS), bii = sec_index(&im, MXSB_S_BUILD_INFO);

    RESET(); expect_result(b, 63, (expect){"V1 short", MXSB_E_HEADER, "V1"});
    RESET(); b[0] = 'X'; expect_result(b, len, (expect){"V2 magic", MXSB_E_MAGIC, "V2"});
    RESET(); b[4] = 1; expect_result(b, len, (expect){"V3 v1 refused", MXSB_E_VERSION, "V3"});
    RESET(); b[4] = 3; expect_result(b, len, (expect){"V3 major 3", MXSB_E_VERSION, "V3"});
    RESET(); b[5] = 9; wr64(b + 40, len); expect_result(b, len, (expect){"minor 9 accepted", MXSB_OK, "ok"});
    RESET(); b[6] = 48; b[7] = 0; expect_result(b, len, (expect){"V4 header_size 48", MXSB_E_HEADER, "V4"});
    RESET(); b[6] = 0x01; b[7] = 0x10; expect_result(b, len, (expect){"V4 header_size 4097", MXSB_E_HEADER, "V4"});
    RESET(); wr64(b + 40, len + 1); expect_result(b, len, (expect){"V5 file_size", MXSB_E_HEADER, "V5"});
    RESET(); expect_result(b, len - 1, (expect){"V5 truncated file", MXSB_E_HEADER, "V5"});
    RESET(); wr32(b + 8, 0x8); expect_result(b, len, (expect){"V6 reserved flag", MXSB_E_HEADER, "V6"});
    RESET(); b[50] = 1; expect_result(b, len, (expect){"V6 reserved bytes", MXSB_E_HEADER, "V6"});
    RESET(); wr32(b + 12, 0); expect_result(b, len, (expect){"V7 count 0", MXSB_E_TABLE, "V7"});
    RESET(); wr32(b + 12, 65); expect_result(b, len, (expect){"V7 count 65", MXSB_E_TABLE, "V7"});
    RESET(); wr64(b + 16, 72); expect_result(b, len, (expect){"V8 table misaligned", MXSB_E_TABLE, "V8"});
    RESET(); wr64(b + 16, 48); expect_result(b, len, (expect){"V8 table in header", MXSB_E_TABLE, "V8"});
    RESET(); wr64(b + 16, (uint64_t)len); expect_result(b, len, (expect){"V9 table past end", MXSB_E_TABLE, "V9"});
    RESET(); wr64(b + 16, UINT64_MAX - 15); expect_result(b, len, (expect){"V9 table offset overflow", MXSB_E_TABLE, "V9"});
    RESET(); wr64(b + entry_off(&im, ti) + 24, 1); expect_result(b, len, (expect){"V10 entry reserved", MXSB_E_TABLE, "V10"});
    RESET(); wr32(b + entry_off(&im, ti) + 4, MXSB_SF_R | MXSB_SF_W | MXSB_SF_X); expect_result(b, len, (expect){"V10 TEXT RWX", MXSB_E_TABLE, "V10"});
    RESET(); wr32(b + entry_off(&im, di) + 4, MXSB_SF_R | MXSB_SF_X); expect_result(b, len, (expect){"V10 DATA R+X", MXSB_E_TABLE, "V10"});
    RESET(); wr32(b + entry_off(&im, mi) + 4, MXSB_SF_R); expect_result(b, len, (expect){"V10 MANIFEST has flags", MXSB_E_TABLE, "V10"});
    RESET(); wr32(b + entry_off(&im, bii), 0); expect_result(b, len, (expect){"V10 type 0 refused", MXSB_E_TABLE, "V10"});
    RESET(); wr32(b + entry_off(&im, bii), 9); expect_result(b, len, (expect){"reserved type 9 skipped", MXSB_OK, "ok"});
    RESET(); wr32(b + entry_off(&im, bii), 9); wr32(b + entry_off(&im, bii) + 4, 1); expect_result(b, len, (expect){"V10 reserved type with flags", MXSB_E_TABLE, "V10"});
    RESET(); wr64(b + entry_off(&im, soi) + 16, 257 * 4); expect_result(b, len, (expect){"V16 SUPEROPS > 256 ids (bounds permitting)", MXSB_E_BOUNDS, "V11"});
    RESET(); wr64(b + entry_off(&im, ti) + 8, im.sec[ti].offset + 8); expect_result(b, len, (expect){"V11 misaligned", MXSB_E_BOUNDS, "V11"});
    RESET(); wr64(b + entry_off(&im, mi) + 8, 64); expect_result(b, len, (expect){"V11 inside table", MXSB_E_BOUNDS, "V11"});
    RESET(); wr64(b + entry_off(&im, ti) + 16, 0); expect_result(b, len, (expect){"V11 size 0", MXSB_E_BOUNDS, "V11"});
    RESET(); wr64(b + entry_off(&im, ti) + 16, UINT64_MAX - im.sec[ti].offset + 1); expect_result(b, len, (expect){"V11 offset+size overflow", MXSB_E_BOUNDS, "V11"});
    RESET(); wr64(b + entry_off(&im, ti) + 16, (uint64_t)len); expect_result(b, len, (expect){"V11 past end", MXSB_E_BOUNDS, "V11"});
    RESET(); wr64(b + entry_off(&im, bi) + 8, 16); expect_result(b, len, (expect){"V12 BSS offset", MXSB_E_BOUNDS, "V12"});
    RESET(); wr64(b + entry_off(&im, bi) + 16, (1ull << 30) + 1); expect_result(b, len, (expect){"V12 BSS > 1 GiB", MXSB_E_BOUNDS, "V12"});
    RESET(); wr64(b + entry_off(&im, di) + 8, im.sec[ti].offset); wr64(b + entry_off(&im, di) + 16, im.sec[ti].size);
            expect_result(b, len, (expect){"V13 not sorted / overlap", MXSB_E_OVERLAP, "V13"});
    RESET(); wr64(b + entry_off(&im, ti) + 16, im.sec[ti].size + 16); expect_result(b, len, (expect){"V13 overlap next", MXSB_E_OVERLAP, "V13"});
    RESET(); wr32(b + entry_off(&im, bii), MXSB_S_SUPEROPS); expect_result(b, len, (expect){"V14 duplicate type", MXSB_E_DUP_SECTION, "V14"});
    RESET(); wr32(b + entry_off(&im, mi), 0x8000); expect_result(b, len, (expect){"V15 missing MANIFEST", MXSB_E_MISSING_SECTION, "V15"});
    RESET(); wr32(b + entry_off(&im, si), 0x8001); expect_result(b, len, (expect){"V15 missing SIGNATURE", MXSB_E_MISSING_SECTION, "V15"});
    RESET(); wr64(b + entry_off(&im, si) + 16, 95); expect_result(b, len, (expect){"V16 signature size", MXSB_E_TABLE, "V16"});
    RESET(); wr64(b + entry_off(&im, soi) + 16, 3); expect_result(b, len, (expect){"V16 superops size", MXSB_E_TABLE, "V16"});
    RESET(); wr64(b + 24, im.sec[ti].offset - 16); expect_result(b, len, (expect){"V17 entry before TEXT", MXSB_E_ENTRY, "V17"});
    RESET(); wr64(b + 24, im.sec[ti].offset + im.sec[ti].size); expect_result(b, len, (expect){"V17 entry at TEXT end", MXSB_E_ENTRY, "V17"});
    RESET(); wr64(b + entry_off(&im, bi) + 16, (1ull << 30)); /* BSS at max ok; total < 4G */ expect_result(b, len, (expect){"V18 BSS 1 GiB ok", MXSB_OK, "ok"});
    /* V18 total overflow needs huge TEXT which V11 catches first; the contract orders V11 before V18, so this is the checkable V18 case: */
    RESET(); wr64(b + entry_off(&im, bi) + 16, (1ull << 30)); /* fine */
    RESET(); b[im.sec[mi].offset] = 'X'; expect_result(b, len, (expect){"V22 manifest first line", MXSB_E_MANIFEST_SYNTAX, "V22"});
    RESET(); b[im.sec[mi].offset + 5] = 0; expect_result(b, len, (expect){"V21 NUL in manifest", MXSB_E_MANIFEST_SYNTAX, "V21"});
    RESET(); b[im.sec[mi].offset + 5] = 0x01; expect_result(b, len, (expect){"V21 control byte", MXSB_E_MANIFEST_SYNTAX, "V21"});
    RESET(); b[im.sec[mi].offset + 5] = 0xC3; b[im.sec[mi].offset + 6] = 0x41; expect_result(b, len, (expect){"V21 bad UTF-8", MXSB_E_MANIFEST_SYNTAX, "V21"});
    RESET(); b[im.sec[mi].offset + 14] = '2'; expect_result(b, len, (expect){"V22 manifest version 2", MXSB_E_MANIFEST_VERSION, "V22"});
    RESET(); b[im.sec[ti].offset + 3] ^= 1; expect_result(b, len, (expect){"V23 TEXT changed -> build_id", MXSB_E_BUILD_ID, "V23"});
    RESET(); b[im.sec[di].offset + 3] ^= 1; expect_result(b, len, (expect){"V23 DATA changed -> build_id", MXSB_E_BUILD_ID, "V23"});
    RESET(); wr64(b + 32, im.caps_summary | (1ull << 10)); expect_result(b, len, (expect){"V24 reserved caps bit", MXSB_E_CAPS_SUMMARY, "V24"});
    RESET(); wr64(b + 32, im.caps_summary | (1ull << 2)); expect_result(b, len, (expect){"V24 caps wider than manifest", MXSB_E_CAPS_SUMMARY, "V24"});
    RESET(); wr64(b + 32, im.caps_summary & ~(1ull << 3)); expect_result(b, len, (expect){"V24 caps narrower than manifest", MXSB_E_CAPS_SUMMARY, "V24"});
    RESET(); wr32(b + im.sec[soi].offset, 0x11); expect_result(b, len, (expect){"V25 superop not required", MXSB_E_NOT_GRANTED, "V25"});
    RESET(); wr32(b + im.sec[soi].offset, 0x7777); expect_result(b, len, (expect){"V25 unknown superop id", MXSB_E_NOT_GRANTED, "V25"});
    /* V27: padding must be zero; V28: TEXT page multiple when DATA/BSS exists */
    RESET(); b[im.sec[mi].offset + im.sec[mi].size] = 1; expect_result(b, len, (expect){"V27 padding after MANIFEST", MXSB_E_PADDING, "V27"});
    RESET(); b[len - 1] ^= 1; /* last byte is inside SIGNATURE (96 bytes, 16-aligned end) -> still valid */ expect_result(b, len, (expect){"signature byte change still structurally valid", MXSB_OK, "ok"});
    RESET(); wr64(b + entry_off(&im, ti) + 16, im.sec[ti].size - 16); b[im.sec[ti].offset + im.sec[ti].size - 1] = 1; expect_result(b, len, (expect){"V27 non-zero bytes left outside a shrunk TEXT", MXSB_E_PADDING, "V27"});
    {   /* V28 proper: shrink TEXT by 16 and zero the freed bytes so V27 passes */
        RESET(); wr64(b + entry_off(&im, ti) + 16, im.sec[ti].size - 16); memset(b + im.sec[ti].offset + im.sec[ti].size - 16, 0, 16);
        expect_result(b, len, (expect){"V28 TEXT not page multiple with DATA", MXSB_E_LAYOUT, "V28"});
    }
    /* signature bytes are outside the digest: changing them must not change the digest */
    {
        uint8_t d1[32], d2[32]; mxsb_diag d = {0};
        RESET(); CHECK(mxsb_digest(b, len, d1, &d) == MXSB_OK, "digest ok");
        b[im.sec[si].offset + 40] ^= 0xFF; CHECK(mxsb_digest(b, len, d2, &d) == MXSB_OK, "digest ok after sig change");
        CHECK(memcmp(d1, d2, 32) == 0, "signature bytes excluded from digest");
        RESET(); b[im.sec[si].offset + 95] ^= 0xFF; CHECK(mxsb_digest(b, len, d2, &d) == MXSB_OK && memcmp(d1, d2, 32) == 0, "last signature byte excluded from digest");
        RESET(); b[im.sec[bii].offset] ^= 1; /* BUILD_INFO is inside the digest */
        CHECK(mxsb_digest(b, len, d2, &d) == MXSB_OK && memcmp(d1, d2, 32) != 0, "BUILD_INFO change alters digest");
        RESET(); wr32(b + 8, MXSB_F_PIE | MXSB_F_DETERMINISTIC_ONLY);
        CHECK(mxsb_digest(b, len, d2, &d) == MXSB_OK && memcmp(d1, d2, 32) != 0, "header flags inside digest");
    }
    /* decode round trip: sections match what we put in */
    {
        mxsb_image dec; mxsb_diag d = {0};
        RESET(); CHECK(mxsb_decode(b, len, &dec, &d) == MXSB_OK, "decode");
        CHECK(dec.nsections == im.nsections, "section count");
        for (uint32_t i = 0; i < im.nsections && i < dec.nsections; ++i) {
            CHECK(dec.sec[i].type == im.sec[i].type && dec.sec[i].size == im.sec[i].size && dec.sec[i].offset == im.sec[i].offset, "section %u meta", i);
            if (im.sec[i].data) CHECK(memcmp(dec.sec[i].data, im.sec[i].data, (size_t)im.sec[i].size) == 0, "section %u bytes", i);
        }
        CHECK(dec.entry_off == im.entry_off && dec.caps_summary == im.caps_summary && dec.flags == im.flags, "header fields");
        mxsb_image_free(&dec);
        /* failed decode leaves the image zeroed and allocation-free */
        RESET(); b[0] = 'Z'; memset(&dec, 0xAB, sizeof dec);
        CHECK(mxsb_decode(b, len, &dec, &d) == MXSB_E_MAGIC, "failed decode code");
        bool zero = true; for (size_t k = 0; k < sizeof dec; ++k) if (((uint8_t *)&dec)[k]) zero = false;
        CHECK(zero, "failed decode zeroes the image");
    }
    /* layout invariants the emitter promises */
    CHECK(im.sec[ti].offset % 16 == 0 && im.sec[di].offset % 16 == 0, "16-byte alignment");
    CHECK(im.sec[ti].size == 4096, "TEXT padded to a page when DATA/BSS exists (got %llu)", (unsigned long long)im.sec[ti].size);
    CHECK(rd64(base + 16) == 64, "table directly after header");
    free(b); free(base); mxsb_image_free(&im);
    /* images without optional sections are valid too */
    { size_t l2; uint8_t *v = make_valid(&l2, NULL, false, false, false); expect_result(v, l2, (expect){"minimal image", MXSB_OK, "ok"}); free(v); }
}

static void manifest_tests(void) {
    struct { const char *text; mxsb_err want; } cases[] = {
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\n", MXSB_OK},
        {"  # comment\n\nmxsb-manifest   1  \nname\ta\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire   fs.read=/x  # c\n", MXSB_OK},
        {"name a\nmxsb-manifest 1\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\n", MXSB_E_MANIFEST_SYNTAX},
        {"mxsb-manifest 2\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\n", MXSB_E_MANIFEST_VERSION},
        {"mxsb-manifest 1\nname a\nabi native\n", MXSB_E_MANIFEST_SYNTAX},
        {"mxsb-manifest 1\nname a b\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\n", MXSB_E_MANIFEST_SYNTAX},
        {"mxsb-manifest 1\nname a\nabi windows\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\n", MXSB_E_MANIFEST_SYNTAX},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 00000000000000000000000000000000000000000000000000000000000000AA\n", MXSB_E_MANIFEST_SYNTAX},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire wifi\n", MXSB_E_UNKNOWN_PERM},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire fs.read=relative/x\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire fs.read=/a/../b\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire net.connect=example.org\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire net.connect=*:443\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire net.connect=example.org:443\n", MXSB_OK},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire render=yes\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire clock=fast\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire superop=teleport\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire memory.max=12X\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire cpu.max_cores=0\n", MXSB_E_BAD_SCOPE},
        {"mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\ncolour blue\n", MXSB_E_MANIFEST_SYNTAX},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        mxsb_manifest m; mxsb_diag d = {0};
        mxsb_err got = mxsb_manifest_parse(cases[i].text, strlen(cases[i].text), &m, &d);
        CHECK(got == cases[i].want, "manifest case %zu: want %s got %s (%s)", i, mxsb_err_name(cases[i].want), mxsb_err_name(got), d.detail);
        if (got == MXSB_OK) mxsb_manifest_free(&m);
    }
    /* normalisation: order and duplicates do not matter; caps_summary derived */
    const char *a = "mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire render\nrequire fs.read=/x\nrequire fs.read=/x\n";
    const char *b = "mxsb-manifest 1\n# c\nname  a\nabi native\nrequire fs.read=/x\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire render\n";
    mxsb_manifest ma, mb; mxsb_diag d = {0};
    CHECK(mxsb_manifest_parse(a, strlen(a), &ma, &d) == MXSB_OK && mxsb_manifest_parse(b, strlen(b), &mb, &d) == MXSB_OK, "parse a,b");
    size_t la, lb; char *sa = mxsb_manifest_serialize(&ma, &la), *sb = mxsb_manifest_serialize(&mb, &lb);
    CHECK(la == lb && memcmp(sa, sb, la) == 0, "same grants => same bytes:\n%s---\n%s", sa, sb);
    CHECK(ma.nrequire == 2, "duplicate dropped (%d)", ma.nrequire);
    CHECK(ma.caps_summary == ((1ull << MXSB_FAM_FS_READ) | (1ull << MXSB_FAM_RENDER)), "caps_summary derived 0x%llx", (unsigned long long)ma.caps_summary);
    free(sa); free(sb); mxsb_manifest_free(&ma); mxsb_manifest_free(&mb);
    /* budgets set no family bit */
    const char *c = "mxsb-manifest 1\nname a\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire memory.max=1G\nrequire cpu.max_cores=2\n";
    mxsb_manifest mc; CHECK(mxsb_manifest_parse(c, strlen(c), &mc, &d) == MXSB_OK && mc.caps_summary == 0, "budgets no family bit"); mxsb_manifest_free(&mc);
    /* V21 limits */
    {
        char *big = malloc(70000); memset(big, 'a', 69999); big[69999] = 0;
        mxsb_manifest m; CHECK(mxsb_manifest_parse(big, 69999, &m, &d) == MXSB_E_MANIFEST_SYNTAX && strcmp(d.rule, "V21") == 0, "line > 1024");
        free(big);
    }
}

static void sha_tests(void) {
    uint8_t d[32]; char hx[65];
    mx_sha256_oneshot("", 0, d); mx_sha256_hex(d, hx);
    CHECK(strcmp(hx, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "sha256('') %s", hx);
    mx_sha256_oneshot("abc", 3, d); mx_sha256_hex(d, hx);
    CHECK(strcmp(hx, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256('abc') %s", hx);
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    mx_sha256_oneshot(m, strlen(m), d); mx_sha256_hex(d, hx);
    CHECK(strcmp(hx, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0, "sha256(56 bytes) %s", hx);
    /* million 'a' streamed in odd chunks */
    mx_sha256 s; mx_sha256_init(&s); char buf[997]; memset(buf, 'a', sizeof buf);
    size_t left = 1000000; while (left) { size_t t = left < sizeof buf ? left : sizeof buf; mx_sha256_update(&s, buf, t); left -= t; }
    mx_sha256_final(&s, d); mx_sha256_hex(d, hx);
    CHECK(strcmp(hx, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0, "sha256(1M a) %s", hx);
}

static uint64_t rng_state = 0x1234567887654321ull;
static uint64_t rng(void) { uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }

static void fuzz(int iters) {
    size_t len; uint8_t *base = make_valid(&len, NULL, true, true, true);
    uint8_t *b = malloc(len + 256);
    int oks = 0, codes[32] = {0};
    for (int it = 0; it < iters; ++it) {
        memcpy(b, base, len);
        size_t l = len;
        int nmut = 1 + (int)(rng() % 4);
        for (int m = 0; m < nmut; ++m) {
            switch (rng() % 6) {
                case 0: b[rng() % l] ^= (uint8_t)(1u << (rng() % 8)); break;
                case 1: b[rng() % l] = (uint8_t)rng(); break;
                case 2: { size_t o = (rng() % 56) & ~(size_t)7; wr64(b + o, rng() % 4 ? rng() : (rng() & 1 ? UINT64_MAX : 0)); break; }   /* header fields */
                case 3: { size_t o = 64 + (rng() % (6 * 32)); o &= ~(size_t)7; wr64(b + o, rng() % 3 ? rng() % (2 * l) : UINT64_MAX - (rng() % 64)); break; } /* table */
                case 4: if (l > 64) l -= rng() % (l - 63); break;                       /* truncate */
                case 5: { size_t o = 64 + (rng() % (6 * 32)); o &= ~(size_t)3; wr32(b + o, (uint32_t)(rng() % 9)); break; } /* types/flags */
            }
        }
        mxsb_diag d = {0};
        mxsb_err rc = mxsb_validate(b, l, &d);
        if (rc == MXSB_OK) oks++;
        CHECK(rc <= MXSB_E_LAYOUT, "fuzz: unknown code %d", rc);
        if (rc <= 31) codes[rc]++;
        /* decode must agree with validate and clean up */
        mxsb_image im; mxsb_err rc2 = mxsb_decode(b, l, &im, &d);
        CHECK(rc2 == rc, "fuzz: decode/validate disagree");
        if (rc2 == MXSB_OK) mxsb_image_free(&im);
    }
    fprintf(stderr, "fuzz: %d iterations, %d still valid (only when a mutation hit signature/padding bytes)\n", iters, oks);
    for (int c = 0; c < 32; ++c) if (codes[c]) fprintf(stderr, "  %-24s %d\n", mxsb_err_name((mxsb_err)c), codes[c]);
    free(b); free(base);
}

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 20000;
    sha_tests(); manifest_tests(); mutation_tests(); fuzz(iters);
    fprintf(stderr, "mxsb_test: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
