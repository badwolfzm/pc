/*
 * asset_store.h — read-only, content-addressed asset store reader with a
 * non-blocking residency cache. Host-side prototype of the storage reader
 * described in "GPU Asset Streaming from Storage".
 *
 * Properties this header promises (see README for what is verified):
 *  - Every open goes through openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS |
 *    RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV) from the store's directory fd.
 *    A symlink anywhere under the store fails with ELOOP; no path string is
 *    validated and then fopen'ed.
 *  - Every chunk is verified against its SHA-256 before it becomes resident.
 *  - No call on the render thread blocks on storage. asset_map() answers
 *    ASSET_NOT_READY for a mip that is not resident.
 *  - A resident buffer is stable between two calls to asset_poll(); eviction
 *    and replacement are applied only inside asset_poll().
 *  - A per-asset generation counter changes whenever a resident mip's bytes
 *    are replaced or the mip is evicted.
 *  - Residency is bounded by hot_set_bytes; a request that cannot fit fails
 *    with ASSET_ERR_BUDGET instead of degrading anything else.
 */
#ifndef WARDEN_ASSET_STORE_H
#define WARDEN_ASSET_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_CHUNK_BYTES   (64u * 1024u)
#define ASSET_MAX_MIPS      16u
#define ASSET_HASH_BYTES    32u

typedef uint32_t asset_handle;

enum {
    ASSET_OK            =  0,
    ASSET_NOT_READY     =  1,   /* not resident yet; never blocks */
    ASSET_ERR_ARG       = -1,
    ASSET_ERR_IO        = -2,   /* errno is set */
    ASSET_ERR_ESCAPE    = -3,   /* openat2 refused the path (symlink, .., xdev) */
    ASSET_ERR_HASH      = -4,   /* bytes on disk do not match the hash */
    ASSET_ERR_FORMAT    = -5,   /* manifest malformed */
    ASSET_ERR_BUDGET    = -6,   /* hot set budget exhausted */
    ASSET_ERR_NOMEM     = -7,
    ASSET_ERR_LIMIT     = -8,   /* too many assets or requests */
};

typedef enum {
    ASSET_PLAN_HDD = 0,   /* one reader, requests sorted by priority then manifest order */
    ASSET_PLAN_SSD = 1,   /* several readers, requests issued as they come */
} asset_plan;

typedef struct {
    size_t     hot_set_bytes;    /* residency budget for decoded mips; 0 = 192 MiB */
    asset_plan plan;
    unsigned   readers;          /* SSD plan worker threads; 0 = 4. HDD plan always 1. */
    unsigned   max_assets;       /* 0 = 256 */
    unsigned   max_pending;      /* 0 = 1024 */
} asset_store_config;

typedef enum { ASSET_KIND_BLOB = 0, ASSET_KIND_IMAGE = 1 } asset_kind;

typedef struct {
    asset_kind kind;
    uint32_t   width, height, stride_px;  /* IMAGE only; ARGB8888, stride in pixels */
    uint32_t   mips;
    uint32_t   resident_mask;             /* bit i set: mip i is resident and mappable */
    uint32_t   wanted_mask;               /* bit i set: mip i is wanted */
    uint32_t   generation;                /* changes when any resident bytes change */
    uint64_t   bytes[ASSET_MAX_MIPS];     /* decoded size of each mip */
} asset_status;

typedef enum {
    ASSET_EV_RESIDENT = 1,   /* mip became resident (or its bytes were replaced) */
    ASSET_EV_EVICTED  = 2,   /* mip was evicted; buffer no longer valid */
    ASSET_EV_FAILED   = 3,   /* read or verify failed; rc holds the ASSET_ERR_* */
} asset_event_kind;

typedef struct {
    asset_event_kind kind;
    asset_handle     asset;
    uint32_t         mip;
    uint32_t         generation;
    int              rc;
} asset_event;

typedef struct asset_store asset_store;

/* Open the store rooted at `root`. The directory is opened once with O_PATH;
 * every later open is relative to that fd and cannot leave it. */
int  asset_store_open(const char *root, const asset_store_config *cfg, asset_store **out);
void asset_store_close(asset_store *s);

/* Read and verify the manifest named by its SHA-256. Synchronous, small, and
 * meant for load time, not the frame loop. */
int  asset_open(asset_store *s, const uint8_t manifest_hash[ASSET_HASH_BYTES], asset_handle *out);
int  asset_close(asset_store *s, asset_handle h);

/* Residency requests. Non-blocking: they queue work for the reader thread(s).
 * priority: 0 = visible now ... 255 = speculative. Lower number wins. */
int  asset_want(asset_store *s, asset_handle h, uint32_t mip, uint8_t priority);
int  asset_drop(asset_store *s, asset_handle h, uint32_t mip);

/* Status and mapping. Never block. The mapping returned by asset_map() is
 * valid until the next asset_poll(). */
int  asset_query(asset_store *s, asset_handle h, asset_status *st);
int  asset_map(asset_store *s, asset_handle h, uint32_t mip, const void **data, size_t *len);

/* Apply completed reads and pending evictions; report what changed. Call once
 * per frame outside the BEGIN_FRAME..PRESENT window. Returns the number of
 * events written (at most max). Never blocks on storage. */
size_t asset_poll(asset_store *s, asset_event *out, size_t max);

/* Counters for the benchmark harness and README numbers. */
typedef struct {
    uint64_t chunks_read, bytes_read, hash_failures, escapes_refused, evictions;
    uint64_t read_ns_total;         /* time spent in pread, all readers */
} asset_store_stats;
void asset_store_stats_get(asset_store *s, asset_store_stats *out);

/* Helpers shared with the packer and tests. */
void asset_sha256(const void *data, size_t len, uint8_t out[ASSET_HASH_BYTES]);
void asset_hash_hex(const uint8_t hash[ASSET_HASH_BYTES], char out[2 * ASSET_HASH_BYTES + 1]);
int  asset_hash_parse(const char *hex, uint8_t out[ASSET_HASH_BYTES]);

/* Open a path beneath the store root with the jail flags. Exposed so the
 * tests can prove the escape behaviour directly. Returns fd or -1 (errno). */
int  asset_store_open_beneath(asset_store *s, const char *relpath);

#ifdef __cplusplus
}
#endif
#endif
