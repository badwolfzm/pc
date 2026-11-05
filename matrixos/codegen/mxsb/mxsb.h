/*
 * mxsb.h -- MXSB v2 super-binary: in-memory image, encoder, decoder,
 * validator (contract rules V1..V25), manifest normaliser.
 *
 * Contract: matrixos/contracts/mxsb-manifest.md 2.0.3 and the schema JSON.
 * Every read of an input buffer goes through a bounds-checked accessor
 * (rd_*: buf, len, offset, width) and fails closed.  All size arithmetic is
 * u64 with explicit overflow checks.  The decoder never allocates from an
 * unchecked field.
 */
#ifndef MXSB_H
#define MXSB_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#define MXSB_MAGIC "MXSB"
#define MXSB_VER_MAJOR 2
#define MXSB_VER_MINOR 0
#define MXSB_HEADER_SIZE 64
#define MXSB_ENTRY_SIZE 32
#define MXSB_MAX_SECTIONS 64
#define MXSB_HEADER_SIZE_MAX 4096
#define MXSB_BSS_MAX (1ull << 30)
#define MXSB_MAPPED_MAX (1ull << 32)
#define MXSB_MANIFEST_MAX 65536
#define MXSB_BUILD_INFO_MAX 65536
#define MXSB_MANIFEST_LINES_MAX 4096
#define MXSB_MANIFEST_LINE_MAX 1024
#define MXSB_SIGNATURE_SIZE 96
#define MXSB_PAGE 4096u

enum mxsb_flag { MXSB_F_PIE = 1u, MXSB_F_DETERMINISTIC_ONLY = 2u, MXSB_F_DEBUG_SECTIONS = 4u, MXSB_F_RESERVED_MASK = 0xFFFFFFF8u };
enum mxsb_sflag { MXSB_SF_R = 1u, MXSB_SF_W = 2u, MXSB_SF_X = 4u };
enum mxsb_stype {
    MXSB_S_MANIFEST = 1, MXSB_S_TEXT = 2, MXSB_S_DATA = 3, MXSB_S_BSS = 4,
    MXSB_S_SUPEROPS = 5, MXSB_S_SIGNATURE = 6, MXSB_S_BUILD_INFO = 7, MXSB_S_VENDOR_MIN = 0x8000
};
/* Error codes: numeric values from the schema file. */
typedef enum {
    MXSB_OK = 0,
    MXSB_E_MAGIC = 1, MXSB_E_VERSION = 2, MXSB_E_HEADER = 3, MXSB_E_TABLE = 4, MXSB_E_BOUNDS = 5,
    MXSB_E_OVERLAP = 6, MXSB_E_MISSING_SECTION = 7, MXSB_E_DUP_SECTION = 8, MXSB_E_ENTRY = 9,
    MXSB_E_DIGEST = 10, MXSB_E_SIGNATURE = 11, MXSB_E_UNTRUSTED_KEY = 12, MXSB_E_MANIFEST_SYNTAX = 13,
    MXSB_E_MANIFEST_VERSION = 14, MXSB_E_UNKNOWN_PERM = 15, MXSB_E_BAD_SCOPE = 16, MXSB_E_BUILD_ID = 17,
    MXSB_E_CAPS_SUMMARY = 18, MXSB_E_NOT_GRANTED = 19, MXSB_E_PADDING = 20, MXSB_E_LAYOUT = 21,
    /* tool-side only (never produced by the loader rules) */
    MXSB_E_IO = 100, MXSB_E_ARG = 101, MXSB_E_TOOLARGE = 102
} mxsb_err;
const char *mxsb_err_name(mxsb_err e);

/* caps_summary family bits (section 5) */
enum mxsb_family {
    MXSB_FAM_FS_READ = 0, MXSB_FAM_FS_WRITE = 1, MXSB_FAM_NET = 2, MXSB_FAM_RENDER = 3, MXSB_FAM_SUPEROP = 4,
    MXSB_FAM_CLOCK = 5, MXSB_FAM_RANDOM = 6, MXSB_FAM_EXEC = 7, MXSB_FAM_PROC_SPAWN = 8, MXSB_FAM_LOAD = 9
};
#define MXSB_CAPS_RESERVED_MASK (~((1ull << 10) - 1ull))

typedef struct {
    uint32_t type, flags;
    uint64_t offset, size;      /* offset as laid out in the file; 0 for BSS */
    uint8_t *data;              /* owned copy (NULL for BSS) */
} mxsb_section;

typedef struct {
    uint8_t ver_major, ver_minor;
    uint16_t header_size;
    uint32_t flags;
    uint64_t entry_off;          /* v2: absolute file offset inside TEXT */
    uint64_t caps_summary;
    uint64_t file_size;
    uint32_t nsections;
    mxsb_section sec[MXSB_MAX_SECTIONS];
    /* decoded helpers */
    uint8_t digest[32];          /* computed over the buffer (decode/validate) */
    bool have_digest;
    /* v1 only */
    bool is_v1;
    uint32_t v1_caps_required, v1_superop_count;
    uint64_t v1_manifest_hash;
} mxsb_image;

typedef struct {
    mxsb_err err;
    const char *rule;           /* "V11" etc., or "emit" */
    char detail[256];
} mxsb_diag;

/* ---- image lifecycle --------------------------------------------------- */
void mxsb_image_init(mxsb_image *im);
void mxsb_image_free(mxsb_image *im);
/* add a section (copies data). Returns index or -1 if full. */
int  mxsb_image_add(mxsb_image *im, uint32_t type, uint32_t flags, const void *data, uint64_t size);
int  mxsb_image_find(const mxsb_image *im, uint32_t type);   /* first index or -1 */

/* ---- layout / encode --------------------------------------------------- */
/* Assign offsets (header, table, sections in a canonical order: by type,
 * vendor sections last in insertion order; 16-byte alignment).  Sets
 * file_size and entry_off from `entry_rel` (offset relative to TEXT start). */
mxsb_err mxsb_layout(mxsb_image *im, uint64_t entry_rel, mxsb_diag *d);
/* Encode into a fresh buffer (caller frees). Runs the validator (V1..V25,
 * signature check skipped when `sig` is NULL/zero) before returning. */
mxsb_err mxsb_encode(const mxsb_image *im, uint8_t **out, size_t *outlen, mxsb_diag *d);
/* digest over an encoded buffer per section 6; the layout must already be valid */
mxsb_err mxsb_digest(const uint8_t *buf, size_t len, uint8_t out[32], mxsb_diag *d);

/* ---- decode / validate ------------------------------------------------- */
/* Structural rules V1..V18, V27, V28, V19 digest compute (not compared), V21..V25.
 * V20 (signature) is left to the caller: the image exposes the digest and the
 * SIGNATURE section; `mxsb_validate` reports MXSB_OK with im->have_digest set.
 * On any failure *im is zeroed and holds no allocation. */
mxsb_err mxsb_decode(const uint8_t *buf, size_t len, mxsb_image *im, mxsb_diag *d);
/* Same checks, no image kept. */
mxsb_err mxsb_validate(const uint8_t *buf, size_t len, mxsb_diag *d);
/* Decode a v1 header (tools only; the loader refuses v1). */
mxsb_err mxsb_decode_v1(const uint8_t *buf, size_t len, mxsb_image *im, mxsb_diag *d);

/* ---- manifest ---------------------------------------------------------- */
typedef struct {
    char name[65];
    char abi[16];
    char build_id[65];
    int nrequire;
    char (*require)[MXSB_MANIFEST_LINE_MAX];   /* normalised "perm[=scope]" lines, sorted, unique */
    uint64_t caps_summary;                      /* derived */
} mxsb_manifest;
void mxsb_manifest_free(mxsb_manifest *m);
/* Parse + normalise (section 4, V21, V22). On success fills *m (caller frees). */
mxsb_err mxsb_manifest_parse(const char *text, size_t len, mxsb_manifest *m, mxsb_diag *d);
/* Serialise the normalised form (caller frees). */
char *mxsb_manifest_serialize(const mxsb_manifest *m, size_t *outlen);
/* family bit for a perm name, or -1 (budgets return -2: no family bit) */
int  mxsb_perm_family(const char *perm);
/* build_id = SHA-256(TEXT || DATA) as hex */
void mxsb_build_id(const uint8_t *text, size_t tlen, const uint8_t *data, size_t dlen, char out[65]);

/* ---- super-op names (provisional table from gpu-commands-ref/rcs.h) ---- */
int  mxsb_superop_id(const char *name);         /* -1 unknown */
const char *mxsb_superop_name(uint32_t id);     /* NULL unknown */

/* ---- dump -------------------------------------------------------------- */
void mxsb_dump(FILE *out, const mxsb_image *im);

#endif
