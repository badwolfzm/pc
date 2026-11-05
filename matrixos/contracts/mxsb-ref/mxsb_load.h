/*
 * mxsb_load.h -- MXSB v2 reference loader (contract-owner implementation).
 *
 * Contract: matrixos/contracts/mxsb-manifest.md 2.0.3, section 9a rules
 * V1..V28, error codes from mxsb-manifest.schema.json.
 *
 * This is the reference the codegen emitter must pass and the kernel ports
 * to Rust. It is deliberately independent of matrixos/codegen/mxsb: own
 * SHA-256, own manifest parser, own bounds-checked reader. The two
 * implementations are compared on the same inputs by run_tests.sh.
 *
 * Properties:
 *  - every read of the input goes through rd_*(buf, len, off, width) and
 *    fails closed; all size arithmetic is u64 with explicit overflow checks;
 *  - the loader performs no allocation while checking; the caller maps the
 *    image after MXSB_OK using the sizes in mxsb_ld_image (V18 bounds them);
 *  - on any failure the result struct is zeroed;
 *  - no I/O, no libc beyond string/stdint, so it can move into a kernel.
 *
 * V20 (signature) is a callback: Ed25519 is not implemented here. The host
 * supplies mxsb_ld_policy.verify; the reference CLI (mxsb_check) wires it to
 * /usr/bin/openssl. Without a verifier only the approvals path can admit a
 * file.
 */
#ifndef MXSB_LOAD_H
#define MXSB_LOAD_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MXSB_LD_CONTRACT "2.0.3"

/* ---- constants from the contract ---------------------------------------- */
#define MXSB_LD_HEADER            64u
#define MXSB_LD_HEADER_MAX        4096u
#define MXSB_LD_ENTRY             32u
#define MXSB_LD_SECTIONS_MAX      64u
#define MXSB_LD_BSS_MAX           (1ull << 30)
#define MXSB_LD_MAPPED_MAX        (1ull << 32)
#define MXSB_LD_MANIFEST_MAX      65536u
#define MXSB_LD_BUILD_INFO_MAX    65536u
#define MXSB_LD_MANIFEST_LINES    4096u
#define MXSB_LD_MANIFEST_LINE     1024u
#define MXSB_LD_SIGNATURE_SIZE    96u
#define MXSB_LD_PAGE              4096ull
#define MXSB_LD_FLAGS_RESERVED    0xFFFFFFF8u
#define MXSB_LD_CAPS_RESERVED     (~((1ull << 10) - 1ull))

enum { MXSB_LD_S_MANIFEST = 1, MXSB_LD_S_TEXT = 2, MXSB_LD_S_DATA = 3, MXSB_LD_S_BSS = 4,
       MXSB_LD_S_SUPEROPS = 5, MXSB_LD_S_SIGNATURE = 6, MXSB_LD_S_BUILD_INFO = 7, MXSB_LD_S_VENDOR_MIN = 0x8000 };
enum { MXSB_LD_SF_R = 1u, MXSB_LD_SF_W = 2u, MXSB_LD_SF_X = 4u };

/* error codes: numeric values from the schema */
typedef enum {
    MXSB_LD_OK = 0,
    MXSB_LD_E_MAGIC = 1, MXSB_LD_E_VERSION = 2, MXSB_LD_E_HEADER = 3, MXSB_LD_E_TABLE = 4,
    MXSB_LD_E_BOUNDS = 5, MXSB_LD_E_OVERLAP = 6, MXSB_LD_E_MISSING_SECTION = 7, MXSB_LD_E_DUP_SECTION = 8,
    MXSB_LD_E_ENTRY = 9, MXSB_LD_E_DIGEST = 10, MXSB_LD_E_SIGNATURE = 11, MXSB_LD_E_UNTRUSTED_KEY = 12,
    MXSB_LD_E_MANIFEST_SYNTAX = 13, MXSB_LD_E_MANIFEST_VERSION = 14, MXSB_LD_E_UNKNOWN_PERM = 15,
    MXSB_LD_E_BAD_SCOPE = 16, MXSB_LD_E_BUILD_ID = 17, MXSB_LD_E_CAPS_SUMMARY = 18, MXSB_LD_E_NOT_GRANTED = 19,
    MXSB_LD_E_PADDING = 20, MXSB_LD_E_LAYOUT = 21
} mxsb_ld_err;
const char *mxsb_ld_err_name(mxsb_ld_err e);

/* ---- grants ------------------------------------------------------------ */
typedef struct {
    char perm[32];
    char scope[MXSB_LD_MANIFEST_LINE];      /* "" when the perm takes none */
} mxsb_ld_grant;

/* What the loader needs from warden.toml (section 9). Parsed by
 * mxsb_ld_container_parse from the [grants] subset, or filled by the host. */
typedef struct {
    mxsb_ld_grant grant[256];                /* fs.read, fs.write, exec, net.connect, net.listen, superop=<name> */
    uint32_t ngrant;
    uint32_t process_spawn_max;              /* process.spawn = { max = N } */
    bool render, random, allow_unsigned;
    char clock[8];                           /* "coarse" | "precise" | "" (none) */
    uint64_t memory_max;                     /* bytes; 0 = not set */
    uint32_t cpu_max_cores;                  /* 0 = not set */
} mxsb_ld_container;

/* ---- policy ------------------------------------------------------------ */
typedef struct {
    const mxsb_ld_container *container;      /* NULL skips V26 (structural check only; never for a real load) */
    const uint8_t (*trusted_key_ids)[32];    /* key ids the host trusts */
    uint32_t ntrusted;
    const uint8_t (*approved_digests)[32];   /* for the unsigned path */
    uint32_t napproved;
    /* Ed25519 verify over the 32-byte digest. Returns true when valid. The
     * key id is given so the host can look up the public key. */
    bool (*verify)(void *ctx, const uint8_t key_id[32], const uint8_t digest[32], const uint8_t sig[64]);
    void *verify_ctx;
    bool skip_signature;                     /* tools only: report digest, do not run V20 */
    bool digest_only;                        /* tools only: stop after V19 with the digest filled in */
} mxsb_ld_policy;

/* ---- result ------------------------------------------------------------ */
typedef struct {
    uint64_t text_off, text_size;            /* file ranges, already proven in-bounds */
    uint64_t data_off, data_size;            /* data_size 0 when absent */
    uint64_t bss_size;
    uint64_t entry_rel;                      /* entry_off - text_off */
    uint64_t mapped_total;                   /* text + data + bss, page-rounded, <= 4 GiB */
    uint32_t flags;
    uint64_t caps_summary;
    uint8_t digest[32];
    uint8_t key_id[32];
    bool signed_ok;                          /* V20 passed via signature (false = approvals path or skipped) */
    char name[65], abi[16], build_id[65];
    mxsb_ld_grant require[MXSB_LD_MANIFEST_LINES]; /* normalised, sorted, unique */
    uint32_t nrequire;
    uint32_t superop_ids[256];
    uint32_t nsuperops;
} mxsb_ld_image;

typedef struct {
    mxsb_ld_err err;
    const char *rule;                        /* "V11" etc. */
    char detail[200];
} mxsb_ld_diag;

/* Run V1..V28 on `buf[0..len]` under `policy`. On MXSB_LD_OK fills *out.
 * On failure *out is zeroed and diag names the rule. Never allocates. */
mxsb_ld_err mxsb_ld_check(const uint8_t *buf, size_t len, const mxsb_ld_policy *policy,
                          mxsb_ld_image *out, mxsb_ld_diag *diag);

/* Parse the [grants] subset of warden.toml that section 9 uses. Lines
 * outside [grants] are ignored. Returns false on a malformed line. */
bool mxsb_ld_container_parse(const char *text, size_t len, mxsb_ld_container *c, char err[128]);

/* SHA-256 (exposed for tools and tests) */
void mxsb_ld_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
typedef struct { uint32_t h[8]; uint64_t nbits; uint8_t buf[64]; uint32_t buflen; } mxsb_ld_sha256_ctx;
void mxsb_ld_sha256_init(mxsb_ld_sha256_ctx *c);
void mxsb_ld_sha256_update(mxsb_ld_sha256_ctx *c, const uint8_t *p, size_t n);
void mxsb_ld_sha256_final(mxsb_ld_sha256_ctx *c, uint8_t out[32]);

/* provisional super-op table (contracts/gpu-commands-ref/rcs.h); -1 unknown */
int mxsb_ld_superop_id(const char *name);
const char *mxsb_ld_superop_name(uint32_t id);

#endif
