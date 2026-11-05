/* MXSB v2 encoder / decoder / validator. See mxsb.h and the contract. */
#include "mxsb.h"
#include "mx_sha256.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ---- diagnostics ------------------------------------------------------- */
static mxsb_err fail(mxsb_diag *d, mxsb_err e, const char *rule, const char *fmt, ...) {
    if (d) {
        d->err = e; d->rule = rule;
        va_list ap; va_start(ap, fmt); vsnprintf(d->detail, sizeof d->detail, fmt, ap); va_end(ap);
    }
    return e;
}
const char *mxsb_err_name(mxsb_err e) {
    switch (e) {
        case MXSB_OK: return "MXSB_OK";
        case MXSB_E_MAGIC: return "MXSB_E_MAGIC"; case MXSB_E_VERSION: return "MXSB_E_VERSION";
        case MXSB_E_HEADER: return "MXSB_E_HEADER"; case MXSB_E_TABLE: return "MXSB_E_TABLE";
        case MXSB_E_BOUNDS: return "MXSB_E_BOUNDS"; case MXSB_E_OVERLAP: return "MXSB_E_OVERLAP";
        case MXSB_E_MISSING_SECTION: return "MXSB_E_MISSING_SECTION"; case MXSB_E_DUP_SECTION: return "MXSB_E_DUP_SECTION";
        case MXSB_E_ENTRY: return "MXSB_E_ENTRY"; case MXSB_E_DIGEST: return "MXSB_E_DIGEST";
        case MXSB_E_SIGNATURE: return "MXSB_E_SIGNATURE"; case MXSB_E_UNTRUSTED_KEY: return "MXSB_E_UNTRUSTED_KEY";
        case MXSB_E_MANIFEST_SYNTAX: return "MXSB_E_MANIFEST_SYNTAX"; case MXSB_E_MANIFEST_VERSION: return "MXSB_E_MANIFEST_VERSION";
        case MXSB_E_UNKNOWN_PERM: return "MXSB_E_UNKNOWN_PERM"; case MXSB_E_BAD_SCOPE: return "MXSB_E_BAD_SCOPE";
        case MXSB_E_BUILD_ID: return "MXSB_E_BUILD_ID"; case MXSB_E_CAPS_SUMMARY: return "MXSB_E_CAPS_SUMMARY";
        case MXSB_E_NOT_GRANTED: return "MXSB_E_NOT_GRANTED"; case MXSB_E_PADDING: return "MXSB_E_PADDING"; case MXSB_E_LAYOUT: return "MXSB_E_LAYOUT";
        case MXSB_E_IO: return "MXSB_E_IO"; case MXSB_E_ARG: return "MXSB_E_ARG"; case MXSB_E_TOOLARGE: return "MXSB_E_TOOLARGE";
    }
    return "MXSB_E_?";
}

/* ---- bounds-checked little-endian accessors ----------------------------- */
static bool rd_ok(size_t len, uint64_t off, uint64_t width) { return off <= len && width <= len - off; }
static bool rd_u16(const uint8_t *b, size_t len, uint64_t off, uint16_t *v) { if (!rd_ok(len, off, 2)) return false; *v = (uint16_t)(b[off] | (b[off+1] << 8)); return true; }
static bool rd_u32(const uint8_t *b, size_t len, uint64_t off, uint32_t *v) {
    if (!rd_ok(len, off, 4)) return false;
    *v = (uint32_t)b[off] | ((uint32_t)b[off+1] << 8) | ((uint32_t)b[off+2] << 16) | ((uint32_t)b[off+3] << 24); return true;
}
static bool rd_u64(const uint8_t *b, size_t len, uint64_t off, uint64_t *v) {
    if (!rd_ok(len, off, 8)) return false;
    uint64_t x = 0; for (int i = 7; i >= 0; --i) x = (x << 8) | b[off + (uint64_t)i]; *v = x; return true;
}
static void wr_u16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void wr_u32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; ++i) b[i] = (uint8_t)(v >> (8*i)); }
static void wr_u64(uint8_t *b, uint64_t v) { for (int i = 0; i < 8; ++i) b[i] = (uint8_t)(v >> (8*i)); }
static bool add_ok(uint64_t a, uint64_t b, uint64_t *out) { if (a > UINT64_MAX - b) return false; *out = a + b; return true; }

/* ---- image ------------------------------------------------------------- */
void mxsb_image_init(mxsb_image *im) { memset(im, 0, sizeof *im); im->ver_major = MXSB_VER_MAJOR; im->ver_minor = MXSB_VER_MINOR; im->header_size = MXSB_HEADER_SIZE; }
void mxsb_image_free(mxsb_image *im) {
    for (uint32_t i = 0; i < im->nsections; ++i) free(im->sec[i].data);
    memset(im, 0, sizeof *im);
}
int mxsb_image_add(mxsb_image *im, uint32_t type, uint32_t flags, const void *data, uint64_t size) {
    if (im->nsections >= MXSB_MAX_SECTIONS) return -1;
    mxsb_section *s = &im->sec[im->nsections];
    memset(s, 0, sizeof *s);
    s->type = type; s->flags = flags; s->size = size;
    if (type != MXSB_S_BSS && size) {
        if (size > (uint64_t)SIZE_MAX / 2) return -1;
        s->data = malloc((size_t)size);
        if (!s->data) return -1;
        memcpy(s->data, data, (size_t)size);
    }
    return (int)im->nsections++;
}
int mxsb_image_find(const mxsb_image *im, uint32_t type) {
    for (uint32_t i = 0; i < im->nsections; ++i) if (im->sec[i].type == type) return (int)i;
    return -1;
}

/* canonical order key: lower sorts earlier; BSS always last */
static int order_key(uint32_t type) {
    switch (type) {
        case MXSB_S_MANIFEST: return 0; case MXSB_S_TEXT: return 1; case MXSB_S_DATA: return 2;
        case MXSB_S_SUPEROPS: return 3; case MXSB_S_BUILD_INFO: return 4; case MXSB_S_SIGNATURE: return 5;
        case MXSB_S_BSS: return 1000;
        default: return 100; /* vendor */
    }
}
static uint64_t align16(uint64_t x) { return (x + 15u) & ~(uint64_t)15u; }

mxsb_err mxsb_layout(mxsb_image *im, uint64_t entry_rel, mxsb_diag *d) {
    if (im->nsections < 1 || im->nsections > MXSB_MAX_SECTIONS) return fail(d, MXSB_E_TABLE, "emit", "section_count %u out of range", im->nsections);
    /* section 3.4 / V28: TEXT is zero-padded to a page multiple when DATA or BSS exists. Callers that
     * compute build_id must do so after mxsb_layout (build_id covers the padded TEXT). */
    if (mxsb_image_find(im, MXSB_S_DATA) >= 0 || mxsb_image_find(im, MXSB_S_BSS) >= 0) {
        int ti = mxsb_image_find(im, MXSB_S_TEXT);
        if (ti >= 0 && im->sec[ti].size % MXSB_PAGE != 0) {
            uint64_t ns = (im->sec[ti].size + MXSB_PAGE - 1) & ~(uint64_t)(MXSB_PAGE - 1);
            uint8_t *nd = realloc(im->sec[ti].data, (size_t)ns);
            if (!nd) return fail(d, MXSB_E_IO, "emit", "out of memory");
            memset(nd + im->sec[ti].size, 0, (size_t)(ns - im->sec[ti].size));
            im->sec[ti].data = nd; im->sec[ti].size = ns;
        }
    }
    /* stable sort by order key */
    for (uint32_t i = 1; i < im->nsections; ++i) {
        mxsb_section t = im->sec[i]; uint32_t j = i;
        while (j > 0 && order_key(im->sec[j-1].type) > order_key(t.type)) { im->sec[j] = im->sec[j-1]; --j; }
        im->sec[j] = t;
    }
    uint64_t table_bytes = (uint64_t)im->nsections * MXSB_ENTRY_SIZE;
    uint64_t off = align16((uint64_t)im->header_size + table_bytes);
    for (uint32_t i = 0; i < im->nsections; ++i) {
        mxsb_section *s = &im->sec[i];
        if (s->type == MXSB_S_BSS) { s->offset = 0; continue; }
        if (s->size == 0) return fail(d, MXSB_E_BOUNDS, "emit", "section type %u is empty (V11 requires size >= 1)", s->type);
        s->offset = off;
        uint64_t end; if (!add_ok(off, s->size, &end)) return fail(d, MXSB_E_BOUNDS, "emit", "offset overflow");
        off = align16(end);
        if (off < end) return fail(d, MXSB_E_BOUNDS, "emit", "offset overflow");
    }
    im->file_size = off;
    int ti = mxsb_image_find(im, MXSB_S_TEXT);
    if (ti < 0) return fail(d, MXSB_E_MISSING_SECTION, "emit", "no TEXT section");
    if (entry_rel >= im->sec[ti].size) return fail(d, MXSB_E_ENTRY, "emit", "entry %llu outside TEXT (size %llu)", (unsigned long long)entry_rel, (unsigned long long)im->sec[ti].size);
    im->entry_off = im->sec[ti].offset + entry_rel;
    return MXSB_OK;
}

static void encode_header(const mxsb_image *im, uint8_t *h) {
    memset(h, 0, im->header_size);
    memcpy(h, MXSB_MAGIC, 4);
    h[4] = im->ver_major; h[5] = im->ver_minor;
    wr_u16(h + 6, im->header_size);
    wr_u32(h + 8, im->flags);
    wr_u32(h + 12, im->nsections);
    wr_u64(h + 16, im->header_size);     /* section_table_off: table directly after header */
    wr_u64(h + 24, im->entry_off);
    wr_u64(h + 32, im->caps_summary);
    wr_u64(h + 40, im->file_size);
}

mxsb_err mxsb_encode(const mxsb_image *im, uint8_t **out, size_t *outlen, mxsb_diag *d) {
    *out = NULL; *outlen = 0;
    if (im->file_size == 0 || im->file_size > (uint64_t)SIZE_MAX / 2) return fail(d, MXSB_E_TOOLARGE, "emit", "layout not run or file too large");
    if (im->header_size % 16 != 0 || im->header_size < MXSB_HEADER_SIZE) return fail(d, MXSB_E_HEADER, "emit", "header_size must be a multiple of 16 and >= 64");
    uint8_t *buf = calloc(1, (size_t)im->file_size);
    if (!buf) return fail(d, MXSB_E_IO, "emit", "out of memory");
    encode_header(im, buf);
    uint8_t *e = buf + im->header_size;
    for (uint32_t i = 0; i < im->nsections; ++i, e += MXSB_ENTRY_SIZE) {
        const mxsb_section *s = &im->sec[i];
        wr_u32(e, s->type); wr_u32(e + 4, s->flags); wr_u64(e + 8, s->offset); wr_u64(e + 16, s->size); wr_u64(e + 24, 0);
        if (s->type != MXSB_S_BSS) memcpy(buf + s->offset, s->data, (size_t)s->size);
    }
    /* Emitter rule: validate our own output before handing it over. */
    mxsb_err rc = mxsb_validate(buf, (size_t)im->file_size, d);
    if (rc != MXSB_OK) { free(buf); return rc; }
    *out = buf; *outlen = (size_t)im->file_size;
    return MXSB_OK;
}

/* ---- decode / validate ------------------------------------------------- */
typedef struct { uint32_t type, flags; uint64_t offset, size; } tentry;

static mxsb_err decode_impl(const uint8_t *buf, size_t len, mxsb_image *im, mxsb_diag *d, bool keep) {
    if (im) mxsb_image_init(im);
    /* V1 */
    if (len < MXSB_HEADER_SIZE) return fail(d, MXSB_E_HEADER, "V1", "file shorter than 64 bytes (%zu)", len);
    /* V2 */
    if (memcmp(buf, MXSB_MAGIC, 4) != 0) return fail(d, MXSB_E_MAGIC, "V2", "bad magic");
    /* V3 */
    uint8_t major = buf[4], minor = buf[5];
    if (major != MXSB_VER_MAJOR) return fail(d, MXSB_E_VERSION, "V3", "ver_major %u (loader accepts only 2)", major);
    /* V4 */
    uint16_t header_size; rd_u16(buf, len, 6, &header_size);
    if (header_size < MXSB_HEADER_SIZE || header_size > MXSB_HEADER_SIZE_MAX || header_size > len)
        return fail(d, MXSB_E_HEADER, "V4", "header_size %u", header_size);
    /* V5 */
    uint64_t file_size; rd_u64(buf, len, 40, &file_size);
    if (file_size != len) return fail(d, MXSB_E_HEADER, "V5", "file_size %llu != length %zu", (unsigned long long)file_size, len);
    /* V6 */
    uint32_t flags; rd_u32(buf, len, 8, &flags);
    if (flags & MXSB_F_RESERVED_MASK) return fail(d, MXSB_E_HEADER, "V6", "reserved flag bits set (0x%08x)", flags);
    for (int i = 48; i < 64; ++i) if (buf[i]) return fail(d, MXSB_E_HEADER, "V6", "reserved header bytes not zero");
    /* V7 */
    uint32_t section_count; rd_u32(buf, len, 12, &section_count);
    if (section_count < 1 || section_count > MXSB_MAX_SECTIONS) return fail(d, MXSB_E_TABLE, "V7", "section_count %u", section_count);
    /* V8 */
    uint64_t table_off; rd_u64(buf, len, 16, &table_off);
    if (table_off % 16 != 0 || table_off < header_size) return fail(d, MXSB_E_TABLE, "V8", "section_table_off %llu", (unsigned long long)table_off);
    /* V9 */
    uint64_t table_bytes = (uint64_t)section_count * MXSB_ENTRY_SIZE, table_end;
    if (!add_ok(table_off, table_bytes, &table_end) || table_end > len) return fail(d, MXSB_E_TABLE, "V9", "section table out of bounds");
    tentry t[MXSB_MAX_SECTIONS];
    for (uint32_t i = 0; i < section_count; ++i) {
        uint64_t eo = table_off + (uint64_t)i * MXSB_ENTRY_SIZE, res;
        if (!rd_u32(buf, len, eo, &t[i].type) || !rd_u32(buf, len, eo + 4, &t[i].flags) ||
            !rd_u64(buf, len, eo + 8, &t[i].offset) || !rd_u64(buf, len, eo + 16, &t[i].size) || !rd_u64(buf, len, eo + 24, &res))
            return fail(d, MXSB_E_TABLE, "V9", "section table read failed");
        /* V10 */
        if (res != 0) return fail(d, MXSB_E_TABLE, "V10", "entry %u reserved != 0", i);
        if (t[i].type == 0) return fail(d, MXSB_E_TABLE, "V10", "entry %u has type 0 (refused; 8..0x7FFF are reserved and skipped)", i);
        bool mem = t[i].type == MXSB_S_TEXT || t[i].type == MXSB_S_DATA || t[i].type == MXSB_S_BSS;
        if (!mem && t[i].flags != 0) return fail(d, MXSB_E_TABLE, "V10", "entry %u (type %u) has flags 0x%x", i, t[i].type, t[i].flags);
        if (t[i].type == MXSB_S_TEXT && t[i].flags != (MXSB_SF_R | MXSB_SF_X)) return fail(d, MXSB_E_TABLE, "V10", "TEXT flags must be R+X");
        if ((t[i].type == MXSB_S_DATA || t[i].type == MXSB_S_BSS) && t[i].flags != (MXSB_SF_R | MXSB_SF_W)) return fail(d, MXSB_E_TABLE, "V10", "DATA/BSS flags must be R+W");
    }
    for (uint32_t i = 0; i < section_count; ++i) {
        if (t[i].type == MXSB_S_BSS) {
            /* V12 */
            if (t[i].offset != 0) return fail(d, MXSB_E_BOUNDS, "V12", "BSS offset != 0");
            if (t[i].size > MXSB_BSS_MAX) return fail(d, MXSB_E_BOUNDS, "V12", "BSS size %llu > 1 GiB", (unsigned long long)t[i].size);
            continue;
        }
        /* V11 */
        if (t[i].offset % 16 != 0) return fail(d, MXSB_E_BOUNDS, "V11", "entry %u offset not 16-aligned", i);
        if (t[i].offset < table_end) return fail(d, MXSB_E_BOUNDS, "V11", "entry %u offset inside header/table", i);
        if (t[i].size < 1) return fail(d, MXSB_E_BOUNDS, "V11", "entry %u size 0", i);
        if (t[i].size > len || t[i].offset > len - t[i].size) return fail(d, MXSB_E_BOUNDS, "V11", "entry %u (type %u) out of bounds", i, t[i].type);
    }
    /* V13: strictly increasing offsets, non-overlapping (excluding BSS) */
    {
        bool have_prev = false; uint64_t prev_end = 0, prev_off = 0;
        for (uint32_t i = 0; i < section_count; ++i) {
            if (t[i].type == MXSB_S_BSS) continue;
            if (have_prev) {
                if (t[i].offset <= prev_off) return fail(d, MXSB_E_OVERLAP, "V13", "section table not sorted by offset");
                if (prev_end > t[i].offset) return fail(d, MXSB_E_OVERLAP, "V13", "sections overlap at entry %u", i);
            }
            have_prev = true; prev_off = t[i].offset; prev_end = t[i].offset + t[i].size; /* cannot overflow after V11 */
        }
    }
    /* V14 */
    int idx[8]; for (int k = 0; k < 8; ++k) idx[k] = -1;
    for (uint32_t i = 0; i < section_count; ++i) {
        if (t[i].type >= 1 && t[i].type <= 7) {
            if (idx[t[i].type] >= 0) return fail(d, MXSB_E_DUP_SECTION, "V14", "section type %u appears twice", t[i].type);
            idx[t[i].type] = (int)i;
        }
    }
    /* V15 */
    if (idx[MXSB_S_MANIFEST] < 0 || idx[MXSB_S_TEXT] < 0 || idx[MXSB_S_SIGNATURE] < 0)
        return fail(d, MXSB_E_MISSING_SECTION, "V15", "MANIFEST, TEXT and SIGNATURE are required");
    /* V16 */
    if (t[idx[MXSB_S_SIGNATURE]].size != MXSB_SIGNATURE_SIZE) return fail(d, MXSB_E_TABLE, "V16", "SIGNATURE size != 96");
    if (idx[MXSB_S_SUPEROPS] >= 0 && (t[idx[MXSB_S_SUPEROPS]].size % 4 != 0 || t[idx[MXSB_S_SUPEROPS]].size > 256 * 4)) return fail(d, MXSB_E_TABLE, "V16", "SUPEROPS size not a multiple of 4 or more than 256 ids");
    if (t[idx[MXSB_S_MANIFEST]].size > MXSB_MANIFEST_MAX) return fail(d, MXSB_E_TABLE, "V16", "MANIFEST > 64 KiB");
    if (idx[MXSB_S_BUILD_INFO] >= 0 && t[idx[MXSB_S_BUILD_INFO]].size > MXSB_BUILD_INFO_MAX) return fail(d, MXSB_E_TABLE, "V16", "BUILD_INFO > 64 KiB");
    /* V17 */
    uint64_t entry_off; rd_u64(buf, len, 24, &entry_off);
    {
        const tentry *tx = &t[idx[MXSB_S_TEXT]];
        if (entry_off < tx->offset || entry_off >= tx->offset + tx->size) return fail(d, MXSB_E_ENTRY, "V17", "entry_off %llu outside TEXT", (unsigned long long)entry_off);
    }
    /* V18 */
    {
        uint64_t total = t[idx[MXSB_S_TEXT]].size, tmp;
        if (idx[MXSB_S_DATA] >= 0) { if (!add_ok(total, t[idx[MXSB_S_DATA]].size, &tmp)) return fail(d, MXSB_E_BOUNDS, "V18", "mapped size overflow"); total = tmp; }
        if (idx[MXSB_S_BSS] >= 0)  { if (!add_ok(total, t[idx[MXSB_S_BSS]].size, &tmp))  return fail(d, MXSB_E_BOUNDS, "V18", "mapped size overflow"); total = tmp; }
        if (total > MXSB_MAPPED_MAX) return fail(d, MXSB_E_BOUNDS, "V18", "mapped size %llu > 4 GiB", (unsigned long long)total);
    }
    /* V27: every byte outside header[0..64), table and sections is zero (padding, 64..header_size) */
    {
        /* regions are: [0,64) header fields, [64, header_size) must be zero, table, each non-BSS section (sorted, V13) */
        uint64_t cursor = MXSB_HEADER_SIZE;
        for (uint64_t k = cursor; k < header_size; ++k) if (buf[k]) return fail(d, MXSB_E_PADDING, "V27", "header extension byte %llu not zero", (unsigned long long)k);
        cursor = header_size;
        for (uint64_t k = cursor; k < table_off; ++k) if (buf[k]) return fail(d, MXSB_E_PADDING, "V27", "padding byte %llu not zero", (unsigned long long)k);
        cursor = table_end;
        for (uint32_t i = 0; i < section_count; ++i) {
            if (t[i].type == MXSB_S_BSS) continue;
            for (uint64_t k = cursor; k < t[i].offset; ++k) if (buf[k]) return fail(d, MXSB_E_PADDING, "V27", "padding byte %llu not zero", (unsigned long long)k);
            cursor = t[i].offset + t[i].size;
        }
        for (uint64_t k = cursor; k < len; ++k) if (buf[k]) return fail(d, MXSB_E_PADDING, "V27", "trailing byte %llu not zero", (unsigned long long)k);
    }
    /* V28: page-multiple TEXT when DATA or BSS exists (section 3.4) */
    if ((idx[MXSB_S_DATA] >= 0 || idx[MXSB_S_BSS] >= 0) && t[idx[MXSB_S_TEXT]].size % MXSB_PAGE != 0)
        return fail(d, MXSB_E_LAYOUT, "V28", "TEXT.size %llu is not a multiple of 4096 but DATA/BSS exists", (unsigned long long)t[idx[MXSB_S_TEXT]].size);
    /* V19: digest = SHA-256(whole file in order, SIGNATURE bytes removed); every range proven in bounds */
    uint8_t digest[32];
    {
        const tentry *ts = &t[idx[MXSB_S_SIGNATURE]];
        mx_sha256 s; mx_sha256_init(&s);
        mx_sha256_update(&s, buf, (size_t)ts->offset);
        mx_sha256_update(&s, buf + ts->offset + ts->size, len - (size_t)(ts->offset + ts->size));
        mx_sha256_final(&s, digest);
    }
    /* V20: signature / approvals -- caller's policy (needs key trust list). */
    /* V21 + V22: manifest */
    mxsb_manifest man; memset(&man, 0, sizeof man);
    {
        const tentry *tm = &t[idx[MXSB_S_MANIFEST]];
        mxsb_err rc = mxsb_manifest_parse((const char *)buf + tm->offset, (size_t)tm->size, &man, d);
        if (rc != MXSB_OK) return rc;
    }
    /* V23: build_id */
    {
        char bid[65];
        const tentry *tx = &t[idx[MXSB_S_TEXT]];
        const tentry *td = idx[MXSB_S_DATA] >= 0 ? &t[idx[MXSB_S_DATA]] : NULL;
        mxsb_build_id(buf + tx->offset, (size_t)tx->size, td ? buf + td->offset : NULL, td ? (size_t)td->size : 0, bid);
        if (strcmp(bid, man.build_id) != 0) { mxsb_manifest_free(&man); return fail(d, MXSB_E_BUILD_ID, "V23", "build_id does not match SHA-256(TEXT||DATA)"); }
    }
    /* V24: caps_summary */
    uint64_t caps_summary; rd_u64(buf, len, 32, &caps_summary);
    if (caps_summary & MXSB_CAPS_RESERVED_MASK) { mxsb_manifest_free(&man); return fail(d, MXSB_E_CAPS_SUMMARY, "V24", "reserved caps bits set"); }
    if (caps_summary != man.caps_summary) { mxsb_manifest_free(&man); return fail(d, MXSB_E_CAPS_SUMMARY, "V24", "caps_summary 0x%llx != manifest 0x%llx", (unsigned long long)caps_summary, (unsigned long long)man.caps_summary); }
    /* V25: every SUPEROPS id has a require superop=<name> line */
    if (idx[MXSB_S_SUPEROPS] >= 0) {
        const tentry *ts = &t[idx[MXSB_S_SUPEROPS]];
        for (uint64_t o = 0; o < ts->size; o += 4) {
            uint32_t id; rd_u32(buf, len, ts->offset + o, &id);
            const char *nm = mxsb_superop_name(id);
            bool found = false;
            if (nm) {
                char want[MXSB_MANIFEST_LINE_MAX]; snprintf(want, sizeof want, "superop=%s", nm);
                for (int k = 0; k < man.nrequire; ++k) if (strcmp(man.require[k], want) == 0) { found = true; break; }
            }
            if (!found) { mxsb_manifest_free(&man); return fail(d, MXSB_E_NOT_GRANTED, "V25", "SUPEROPS id %u (%s) has no matching require line", id, nm ? nm : "unknown"); }
        }
    }
    mxsb_manifest_free(&man);
    /* V26 (container manifest attenuation) needs warden.toml: loader policy, not done here. */

    if (keep && im) {
        im->ver_major = major; im->ver_minor = minor; im->header_size = header_size; im->flags = flags;
        im->entry_off = entry_off; im->caps_summary = caps_summary; im->file_size = file_size;
        for (uint32_t i = 0; i < section_count; ++i) {
            int k = mxsb_image_add(im, t[i].type, t[i].flags, t[i].type == MXSB_S_BSS ? NULL : buf + t[i].offset, t[i].size);
            if (k < 0) { mxsb_image_free(im); return fail(d, MXSB_E_IO, "decode", "out of memory"); }
            im->sec[k].offset = t[i].offset;
        }
        memcpy(im->digest, digest, 32); im->have_digest = true;
    }
    if (d) { d->err = MXSB_OK; d->rule = "ok"; d->detail[0] = 0; }
    return MXSB_OK;
}
mxsb_err mxsb_decode(const uint8_t *buf, size_t len, mxsb_image *im, mxsb_diag *d) {
    mxsb_err rc = decode_impl(buf, len, im, d, true);
    if (rc != MXSB_OK && im) memset(im, 0, sizeof *im);
    return rc;
}
mxsb_err mxsb_validate(const uint8_t *buf, size_t len, mxsb_diag *d) { return decode_impl(buf, len, NULL, d, false); }

mxsb_err mxsb_digest(const uint8_t *buf, size_t len, uint8_t out[32], mxsb_diag *d) {
    mxsb_image im; mxsb_err rc = mxsb_decode(buf, len, &im, d);
    if (rc != MXSB_OK) return rc;
    memcpy(out, im.digest, 32); mxsb_image_free(&im); return MXSB_OK;
}

/* ---- v1 (tools only) ----------------------------------------------------- */
mxsb_err mxsb_decode_v1(const uint8_t *buf, size_t len, mxsb_image *im, mxsb_diag *d) {
    mxsb_image_init(im);
    if (len < 32 || memcmp(buf, MXSB_MAGIC, 4) != 0) return fail(d, MXSB_E_MAGIC, "v1", "not an MXSB file");
    uint16_t version; rd_u16(buf, len, 4, &version);
    if (version != 1) return fail(d, MXSB_E_VERSION, "v1", "version %u is not 1", version);
    uint32_t text_size, data_size, superop_count, caps; uint64_t entry_off, mh;
    rd_u32(buf, len, 8, &text_size); rd_u32(buf, len, 12, &data_size); rd_u32(buf, len, 16, &superop_count); rd_u32(buf, len, 20, &caps);
    rd_u64(buf, len, 24, &entry_off); mh = 0;
    /* v1 packed header: magic4 ver2 flags2 text4 data4 sop4 caps4 entry8 hash8 = 40 bytes. The contract calls it 32;
       we follow the struct (40) and tolerate 32 by treating a short file as hash-less. */
    uint64_t hdr = 40;
    if (len >= 40) rd_u64(buf, len, 32, &mh); else { hdr = 32; mh = 0; }
    uint64_t need;
    if (!add_ok(hdr, text_size, &need) || !add_ok(need, data_size, &need) || need > len)
        return fail(d, MXSB_E_BOUNDS, "v1", "text/data sizes exceed file (text %u data %u len %zu)", text_size, data_size, len);
    im->is_v1 = true; im->ver_major = 1; im->ver_minor = 0; im->header_size = (uint16_t)hdr;
    im->v1_caps_required = caps; im->v1_superop_count = superop_count; im->v1_manifest_hash = mh; im->entry_off = entry_off; im->file_size = len;
    if (text_size) mxsb_image_add(im, MXSB_S_TEXT, MXSB_SF_R | MXSB_SF_X, buf + hdr, text_size);
    if (data_size) mxsb_image_add(im, MXSB_S_DATA, MXSB_SF_R | MXSB_SF_W, buf + hdr + text_size, data_size);
    im->caps_summary = caps & 0x7Fu;   /* bits 0..6 keep their positions */
    return MXSB_OK;
}

/* ---- manifest ------------------------------------------------------------ */
void mxsb_manifest_free(mxsb_manifest *m) { free(m->require); memset(m, 0, sizeof *m); }

static const struct { const char *name; int family; int scope; } perms[] = {
    /* scope kinds: 0 none, 1 abs path prefix, 2 host:port, 3 superop name, 4 clock enum, 5 size, 6 uint */
    {"load", MXSB_FAM_LOAD, 0}, {"fs.read", MXSB_FAM_FS_READ, 1}, {"fs.write", MXSB_FAM_FS_WRITE, 1}, {"exec", MXSB_FAM_EXEC, 1},
    {"net.connect", MXSB_FAM_NET, 2}, {"net.listen", MXSB_FAM_NET, 2}, {"proc.spawn", MXSB_FAM_PROC_SPAWN, 0},
    {"render", MXSB_FAM_RENDER, 0}, {"superop", MXSB_FAM_SUPEROP, 3}, {"clock", MXSB_FAM_CLOCK, 4}, {"random", MXSB_FAM_RANDOM, 0},
    {"memory.max", -2, 5}, {"cpu.max_cores", -2, 6},
};
int mxsb_perm_family(const char *perm) {
    for (size_t i = 0; i < sizeof perms / sizeof perms[0]; ++i) if (strcmp(perm, perms[i].name) == 0) return perms[i].family;
    return -1;
}
static int perm_scope_kind(const char *perm) {
    for (size_t i = 0; i < sizeof perms / sizeof perms[0]; ++i) if (strcmp(perm, perms[i].name) == 0) return perms[i].scope;
    return -1;
}
static bool path_has_dotdot(const char *p) {
    /* segment-wise check, bounded by the string length (review finding 4) */
    size_t n = strlen(p);
    for (size_t i = 0; i < n; ) {
        size_t j = i; while (j < n && p[j] != '/') ++j;
        if (j - i == 2 && p[i] == '.' && p[i+1] == '.') return true;
        i = j + 1;
    }
    return false;
}
static bool valid_host_port(const char *s, bool allow_wild_host, bool *out_wild) {
    const char *c = strrchr(s, ':');
    if (!c || c == s || !c[1]) return false;
    for (const char *p = c + 1; *p; ++p) if (!isdigit((unsigned char)*p)) return false;
    long port = strtol(c + 1, NULL, 10);
    if (port < 1 || port > 65535) return false;
    size_t hl = (size_t)(c - s);
    if (hl == 1 && s[0] == '*') { *out_wild = true; return allow_wild_host; }
    *out_wild = false;
    for (size_t i = 0; i < hl; ++i) { char ch = s[i]; if (!(isalnum((unsigned char)ch) || ch == '.' || ch == '-' || ch == ':' || ch == '[' || ch == ']')) return false; }
    return true;
}
static mxsb_err check_scope(const char *perm, const char *scope, int lineno, mxsb_diag *d) {
    int kind = perm_scope_kind(perm);
    switch (kind) {
        case 0: if (scope) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: %s takes no scope", lineno, perm); return MXSB_OK;
        case 1:
            if (!scope || scope[0] != '/') return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: %s needs an absolute path", lineno, perm);
            if (path_has_dotdot(scope)) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: path contains '..'", lineno);
            return MXSB_OK;
        case 2: {
            bool wild = false;
            if (!scope || !valid_host_port(scope, false, &wild)) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: %s needs host:port (no '*' in a binary)", lineno, perm);
            return MXSB_OK;
        }
        case 3: if (!scope || mxsb_superop_id(scope) < 0) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: unknown super-op '%s'", lineno, scope ? scope : ""); return MXSB_OK;
        case 4: if (!scope || (strcmp(scope, "coarse") && strcmp(scope, "precise"))) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: clock scope must be coarse|precise", lineno); return MXSB_OK;
        case 5: {
            if (!scope || !isdigit((unsigned char)scope[0])) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: memory.max needs a size", lineno);
            const char *p = scope; while (isdigit((unsigned char)*p)) ++p;
            if (*p && !((*p == 'K' || *p == 'M' || *p == 'G') && !p[1])) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: bad size suffix", lineno);
            return MXSB_OK;
        }
        case 6: {
            if (!scope || !isdigit((unsigned char)scope[0])) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: cpu.max_cores needs an integer", lineno);
            for (const char *p = scope; *p; ++p) if (!isdigit((unsigned char)*p)) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: cpu.max_cores needs an integer", lineno);
            if (strtoull(scope, NULL, 10) == 0) return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: cpu.max_cores must be > 0", lineno);
            return MXSB_OK;
        }
        default: return fail(d, MXSB_E_UNKNOWN_PERM, "V22", "line %d: unknown permission '%s'", lineno, perm);
    }
}
static int cmp_lines(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

mxsb_err mxsb_manifest_parse(const char *text, size_t len, mxsb_manifest *m, mxsb_diag *d) {
    memset(m, 0, sizeof *m);
    /* V21: UTF-8, no NUL, no control chars other than \n and \t, line limits */
    size_t nlines = 0, linelen = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0) return fail(d, MXSB_E_MANIFEST_SYNTAX, "V21", "NUL byte at %zu", i);
        if (c == '\n') { nlines++; linelen = 0; continue; }
        if (c < 0x20 && c != '\t') return fail(d, MXSB_E_MANIFEST_SYNTAX, "V21", "control byte 0x%02x at %zu", c, i);
        if (c >= 0x80) {   /* UTF-8 sequence check */
            int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
            if (extra < 0 || i + (size_t)extra >= len) return fail(d, MXSB_E_MANIFEST_SYNTAX, "V21", "bad UTF-8 at %zu", i);
            for (int k = 1; k <= extra; ++k) if (((unsigned char)text[i + (size_t)k] & 0xC0) != 0x80) return fail(d, MXSB_E_MANIFEST_SYNTAX, "V21", "bad UTF-8 at %zu", i);
            linelen += (size_t)extra; i += (size_t)extra;
        }
        if (++linelen > MXSB_MANIFEST_LINE_MAX) return fail(d, MXSB_E_MANIFEST_SYNTAX, "V21", "line longer than 1024 bytes");
        if (nlines >= MXSB_MANIFEST_LINES_MAX) return fail(d, MXSB_E_MANIFEST_SYNTAX, "V21", "more than 4096 lines");
    }
    /* V22: grammar */
    m->require = calloc(MXSB_MANIFEST_LINES_MAX, MXSB_MANIFEST_LINE_MAX);
    if (!m->require) return fail(d, MXSB_E_IO, "V22", "out of memory");
    bool saw_schema = false, saw_name = false, saw_abi = false, saw_bid = false;
    int lineno = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t e = pos; while (e < len && text[e] != '\n') ++e;
        lineno++;
        /* copy, cut comment, trim, collapse whitespace */
        char line[MXSB_MANIFEST_LINE_MAX + 1]; size_t n = 0;
        bool in_ws = true;
        for (size_t i = pos; i < e; ++i) {
            char c = text[i];
            if (c == '#') break;
            if (c == ' ' || c == '\t') { if (!in_ws) { line[n++] = ' '; in_ws = true; } continue; }
            line[n++] = c; in_ws = false;
        }
        while (n && line[n-1] == ' ') --n;
        line[n] = 0;
        pos = e + 1;
        if (n == 0) continue;
        char *sp = strchr(line, ' ');
        const char *key = line; const char *val = NULL;
        if (sp) { *sp = 0; val = sp + 1; }
        if (!saw_schema) {
            if (strcmp(key, "mxsb-manifest") != 0) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: first line must be 'mxsb-manifest 1'", lineno); }
            if (!val || strcmp(val, "1") != 0) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_VERSION, "V22", "line %d: unsupported manifest version '%s'", lineno, val ? val : ""); }
            saw_schema = true; continue;
        }
        if (strcmp(key, "mxsb-manifest") == 0) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: duplicate schema line", lineno); }
        if (strcmp(key, "name") == 0) {
            if (saw_name || !val || strchr(val, ' ')) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: bad or duplicate name", lineno); }
            size_t vl = strlen(val); if (vl < 1 || vl > 64) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: name length", lineno); }
            for (const char *p = val; *p; ++p) if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '-')) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: bad name character", lineno); }
            memcpy(m->name, val, vl + 1); saw_name = true; continue;
        }
        if (strcmp(key, "abi") == 0) {
            if (saw_abi || !val || (strcmp(val, "native") && strcmp(val, "linux-compat"))) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: abi must be native|linux-compat", lineno); }
            snprintf(m->abi, sizeof m->abi, "%s", val); saw_abi = true; continue;
        }
        if (strcmp(key, "build_id") == 0) {
            if (saw_bid || !val || strlen(val) != 64) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: build_id must be 64 hex chars", lineno); }
            for (const char *p = val; *p; ++p) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: build_id must be lowercase hex", lineno); }
            memcpy(m->build_id, val, 65); saw_bid = true; continue;
        }
        if (strcmp(key, "require") == 0) {
            if (!val || strchr(val, ' ')) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: require needs one <perm>[=<scope>]", lineno); }
            char perm[MXSB_MANIFEST_LINE_MAX]; snprintf(perm, sizeof perm, "%s", val);
            char *eq = strchr(perm, '='); const char *scope = NULL;
            if (eq) { *eq = 0; scope = eq + 1; if (!*scope) { mxsb_manifest_free(m); return fail(d, MXSB_E_BAD_SCOPE, "V22", "line %d: empty scope", lineno); } }
            if (mxsb_perm_family(perm) == -1) { mxsb_manifest_free(m); return fail(d, MXSB_E_UNKNOWN_PERM, "V22", "line %d: unknown permission '%s'", lineno, perm); }
            mxsb_err rc = check_scope(perm, scope, lineno, d);
            if (rc != MXSB_OK) { mxsb_manifest_free(m); return rc; }
            if (m->nrequire >= MXSB_MANIFEST_LINES_MAX) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "too many require lines"); }
            snprintf(m->require[m->nrequire++], MXSB_MANIFEST_LINE_MAX, "%s", val);
            continue;
        }
        mxsb_manifest_free(m);
        return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "line %d: unknown key '%s'", lineno, key);
    }
    if (!saw_schema) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "empty manifest"); }
    if (!saw_name || !saw_abi || !saw_bid) { mxsb_manifest_free(m); return fail(d, MXSB_E_MANIFEST_SYNTAX, "V22", "name, abi and build_id are required"); }
    /* normalise: sort bytewise, drop duplicates */
    qsort(m->require, (size_t)m->nrequire, MXSB_MANIFEST_LINE_MAX, cmp_lines);
    int w = 0;
    for (int i = 0; i < m->nrequire; ++i) if (w == 0 || strcmp(m->require[w-1], m->require[i]) != 0) { if (w != i) memcpy(m->require[w], m->require[i], MXSB_MANIFEST_LINE_MAX); ++w; }
    m->nrequire = w;
    /* derived caps_summary */
    m->caps_summary = 0;
    for (int i = 0; i < m->nrequire; ++i) {
        char perm[MXSB_MANIFEST_LINE_MAX]; snprintf(perm, sizeof perm, "%s", m->require[i]);
        char *eq = strchr(perm, '='); if (eq) *eq = 0;
        int fam = mxsb_perm_family(perm);
        if (fam >= 0) m->caps_summary |= 1ull << fam;
    }
    return MXSB_OK;
}

char *mxsb_manifest_serialize(const mxsb_manifest *m, size_t *outlen) {
    size_t cap = 256 + (size_t)m->nrequire * (MXSB_MANIFEST_LINE_MAX + 10);
    char *out = malloc(cap); if (!out) return NULL;
    int n = snprintf(out, cap, "mxsb-manifest 1\nname %s\nabi %s\nbuild_id %s\n", m->name, m->abi, m->build_id);
    for (int i = 0; i < m->nrequire; ++i) n += snprintf(out + n, cap - (size_t)n, "require %s\n", m->require[i]);
    if (outlen) *outlen = (size_t)n;
    return out;
}

void mxsb_build_id(const uint8_t *text, size_t tlen, const uint8_t *data, size_t dlen, char out[65]) {
    mx_sha256 s; uint8_t dg[32];
    mx_sha256_init(&s); mx_sha256_update(&s, text, tlen); if (data && dlen) mx_sha256_update(&s, data, dlen); mx_sha256_final(&s, dg);
    mx_sha256_hex(dg, out);
}

/* ---- super-op names: PROVISIONAL, taken from contracts/gpu-commands-ref/rcs.h
 * (enum rcs_opcode).  To be replaced by the table in gpu-commands.md when the
 * GPU command-set thread publishes it. */
static const struct { const char *name; uint32_t id; } superops[] = {
    {"create_surface", 0x01}, {"destroy_surface", 0x02}, {"bind_asset", 0x03}, {"upload", 0x04},
    {"set_clip", 0x10}, {"clear", 0x11}, {"fill_rect", 0x12}, {"blit", 0x13}, {"line", 0x14}, {"draw_triangles", 0x15},
    {"begin_frame", 0x20}, {"present", 0x21}, {"fence", 0x22}, {"promise_drop", 0x23},
};
int mxsb_superop_id(const char *name) {
    for (size_t i = 0; i < sizeof superops / sizeof superops[0]; ++i) if (strcmp(name, superops[i].name) == 0) return (int)superops[i].id;
    return -1;
}
const char *mxsb_superop_name(uint32_t id) {
    for (size_t i = 0; i < sizeof superops / sizeof superops[0]; ++i) if (superops[i].id == id) return superops[i].name;
    return NULL;
}

/* ---- dump ---------------------------------------------------------------- */
static const char *stype_name(uint32_t t) {
    switch (t) { case 1: return "MANIFEST"; case 2: return "TEXT"; case 3: return "DATA"; case 4: return "BSS"; case 5: return "SUPEROPS"; case 6: return "SIGNATURE"; case 7: return "BUILD_INFO"; default: return t >= 0x8000 ? "VENDOR" : "?"; }
}
void mxsb_dump(FILE *out, const mxsb_image *im) {
    fprintf(out, "mxsb v%u.%u header_size=%u flags=0x%x sections=%u entry_off=%llu caps_summary=0x%llx file_size=%llu%s\n",
            im->ver_major, im->ver_minor, im->header_size, im->flags, im->nsections, (unsigned long long)im->entry_off,
            (unsigned long long)im->caps_summary, (unsigned long long)im->file_size, im->is_v1 ? " (v1: tools decode, loader refuses)" : "");
    if (im->is_v1) fprintf(out, "  v1 caps_required=0x%x superop_count=%u manifest_hash(fnv1a)=0x%016llx\n", im->v1_caps_required, im->v1_superop_count, (unsigned long long)im->v1_manifest_hash);
    if (im->have_digest) { char hx[65]; mx_sha256_hex(im->digest, hx); fprintf(out, "  digest sha256=%s\n", hx); }
    for (uint32_t i = 0; i < im->nsections; ++i) {
        const mxsb_section *s = &im->sec[i];
        fprintf(out, "  [%2u] %-10s type=%-5u flags=%c%c%c offset=%-8llu size=%llu\n", i, stype_name(s->type), s->type,
                (s->flags & MXSB_SF_R) ? 'R' : '-', (s->flags & MXSB_SF_W) ? 'W' : '-', (s->flags & MXSB_SF_X) ? 'X' : '-',
                (unsigned long long)s->offset, (unsigned long long)s->size);
        if ((s->type == MXSB_S_MANIFEST || s->type == MXSB_S_BUILD_INFO) && s->data) {
            for (uint64_t k = 0; k < s->size; ) {
                uint64_t e = k; while (e < s->size && s->data[e] != '\n') ++e;
                fprintf(out, "       | %.*s\n", (int)(e - k), (const char *)s->data + k);
                k = e + 1;
            }
        }
        if (s->type == MXSB_S_SUPEROPS && s->data) {
            fprintf(out, "       |");
            for (uint64_t k = 0; k + 4 <= s->size; k += 4) { uint32_t id; rd_u32(s->data, (size_t)s->size, k, &id); const char *nm = mxsb_superop_name(id); fprintf(out, " %u(%s)", id, nm ? nm : "?"); }
            fprintf(out, "\n");
        }
        if (s->type == MXSB_S_SIGNATURE && s->data) {
            bool zero = true; for (int k = 0; k < 96; ++k) if (s->data[k]) zero = false;
            if (zero) fprintf(out, "       | unsigned (all zero): admitted only via approvals list + allow_unsigned\n");
            else { char kid[65]; mx_sha256_hex(s->data, kid); fprintf(out, "       | key_id=%s\n", kid); }
        }
    }
}
