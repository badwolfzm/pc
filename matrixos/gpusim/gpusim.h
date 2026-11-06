/*
 * gpusim.h - simulated memory-based GPU (prototype).
 *
 * The "GPU" is the render service from contracts/gpu-commands.md running on a
 * CPU core: surfaces live in CPU RAM, the 14-command RCS ring is executed by
 * the reference validator (rcs.c) with its pixel loops on the SIMD super-op
 * kernels (scalar / 128-bit SSE2-NEON / AVX2), and granted assets come from the
 * on-disk asset store (openat2 jail + per-chunk SHA-256, assets/asset_store.c).
 *
 * Data flow per frame:
 *   client encodes a batch -> gpusim_submit copies it out (copy, then check)
 *   -> rcs_submit validates every command, then executes -> PRESENT hands the
 *   back buffer + dirty-tile bitmap to the scanout step, which copies only the
 *   dirty 16x16 tiles into the front buffer ("display memory").
 *
 * Assets: gpusim_grant() is the policy side. It opens a manifest by hash and
 * queues mip 0; gpusim_poll() (between frames, never inside BEGIN..PRESENT)
 * applies finished reads and refreshes the asset table pointers. Until an
 * asset is resident, BIND writes zeros and DRAW_TRIANGLES draws nothing, as in
 * the reference (contract open point 6). The frame path never reads the disk.
 */
#ifndef GPUSIM_H
#define GPUSIM_H

#include "../contracts/gpu-commands-ref/rcs.h"
#include "../assets/asset_store.h"
#include "../superops/mx_superops.h"

typedef struct {
    const char *store_root;   /* asset store directory, or NULL for no store */
    asset_plan  plan;         /* HDD (1 reader) or SSD (several) */
    mxs_tier    tier;         /* MXS_TIER_AUTO picks AVX2 when present */
    uint32_t    rights;       /* render capability rights; RCS_R_ALL for the demo */
} gpusim_config;

typedef struct {
    uint64_t frames, batches, rejected;
    uint64_t submit_ns, scanout_ns;    /* totals */
    uint64_t scanout_tiles;
} gpusim_stats;

typedef struct gpusim gpusim;

int  gpusim_open(const gpusim_config *cfg, gpusim **out);
void gpusim_close(gpusim *g);

/* Policy side: put asset `manifest_hash` in asset slot `slot` as IMAGE or MESH.
 * IMAGE: ARGB8888, linear (not --tiles), stride == width.
 * MESH:  a blob of tri_count * 7 little-endian int32 (x0,y0,x1,y1,x2,y2 in 16.16, colour).
 * Returns ASSET_OK or an ASSET_ERR_* (geometry the RCS asset table cannot express is ASSET_ERR_FORMAT). */
int  gpusim_grant(gpusim *g, uint32_t slot, const uint8_t manifest_hash[ASSET_HASH_BYTES], uint32_t rcs_kind);

/* Apply finished asset reads; refresh the asset table. Call between frames.
 * Returns the number of granted assets that are resident. */
uint32_t gpusim_poll(gpusim *g);

/* Submit one batch exactly as the ring delivered it. The bytes are copied into
 * the service's private buffer before any field is read. */
rcs_result gpusim_submit(gpusim *g, const uint8_t *bytes, size_t n);

/* Front buffer after the last PRESENT (w*h ARGB8888), or NULL before the first. */
const uint32_t *gpusim_front(const gpusim *g, uint32_t *w, uint32_t *h);
void gpusim_stats_get(const gpusim *g, gpusim_stats *out);
const rcs_client *gpusim_client(const gpusim *g);

/* ---- client-side batch encoder (what a game links) ---- */
typedef struct { uint8_t buf[RCS_MAX_BATCH_BYTES]; size_t len; uint32_t count; int overflow; } gpusim_batch;
void gpusim_batch_begin(gpusim_batch *b, uint8_t kind);
void gpusim_batch_add(gpusim_batch *b, uint16_t op, const void *body, uint32_t body_len, const void *payload, uint32_t payload_len);
#define GPUSIM_ADD(b, op, s) gpusim_batch_add((b), (op), &(s), (uint32_t)sizeof(s), NULL, 0)
/* Patches the header; returns the batch length, or 0 if it overflowed. */
size_t gpusim_batch_end(gpusim_batch *b);

#endif
