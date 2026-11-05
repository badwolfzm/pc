/*
 * mx_render.h - the render service: rings in, pixels out.
 *
 * Connects three modules that were written against each other's contracts
 * but never linked before:
 *   contracts/gpu-commands-ref/rcs.c   validates and executes a batch (slots, budgets, rights)
 *   canvas/ghost_canvas.c              turns a PRESENT into the set of 16x16 tiles that changed
 *   assets/asset_store.c               streams granted assets from a jailed, hash-verified store
 *
 * Per client the service owns: one ring (mx_ring.h), one rcs_client (the
 * surface and asset tables), one asset grant table (which store handle each
 * asset slot names) and one display canvas. The client never sees a pointer,
 * a path or a hash: policy grants assets by slot (mx_render_grant_*), the
 * client binds slots to surfaces, draws, presents, and the service hands the
 * changed tiles of the presented surface to the sink (encoder/streamer).
 *
 * Frame flow in mx_render_pump():
 *   1. asset_poll(): apply completed reads; refresh the asset table pointers
 *      (asset_map is stable until the next poll) and the ring's ready_mask.
 *   2. For each client, take every published batch: copy out, rcs_submit,
 *      write the result. On PRESENT: copy the damaged tiles of the surface
 *      into the canvas, import the damage bits, gc_present -> sink.
 * Everything runs on the caller's thread; the store's reader threads are the
 * only other threads and they never touch client state.
 */
#ifndef MX_RENDER_H
#define MX_RENDER_H
#include "mx_ring.h"
#include "../canvas/ghost_canvas.h"
#include "../assets/asset_store.h"

#define MX_RENDER_MAX_CLIENTS 8u

/* Receives the changed tiles of a presented frame (the encoder's input).
 * Same signature as gc_sink_fn plus the client and frame number. */
typedef struct mx_render_sink {
    int (*tile)(struct mx_render_sink *self, uint32_t client, uint32_t frame_no,
                uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride);
    void (*frame_done)(struct mx_render_sink *self, uint32_t client, uint32_t frame_no, const gc_stats *st, uint32_t canvas_w, uint32_t canvas_h);
    void *user;
} mx_render_sink;

typedef struct {
    bool          granted;
    asset_handle  h;
    uint32_t      mip;
    uint32_t      kind;      /* rcs_asset_kind */
    bool          resident;
} mx_render_grant;

typedef struct {
    uint64_t batches, rejected, presents, bad_descs;
    uint64_t tiles_emitted, bytes_emitted, pixel_ops;
    uint64_t ns_copy, ns_submit, ns_composite;   /* accumulated service time by stage */
    uint64_t last_present_ns;                    /* submit + composite of the last PRESENT batch */
} mx_render_client_stats;

typedef struct {
    bool            live;
    uint64_t        badge;
    mx_ring        *ring;
    rcs_client      rcs;
    mx_render_grant grants[RCS_MAX_ASSETS];
    gc_canvas       canvas;        /* sized to the presented surface; re-created when it changes */
    bool            canvas_live;
    uint32_t        display_slot;
    mx_render_client_stats stats;
} mx_render_client;

typedef struct {
    asset_store     *store;        /* may be NULL: no assets, UPLOAD only */
    mx_render_sink  *sink;
    mx_render_client clients[MX_RENDER_MAX_CLIENTS];
    uint8_t         *scratch;      /* RCS_MAX_BATCH_BYTES: the private copy of the batch */
    asset_event      events[256];
    uint64_t         polls;
} mx_render_service;

int  mx_render_init(mx_render_service *s, asset_store *store, mx_render_sink *sink);
void mx_render_free(mx_render_service *s);

/* Attach a client: its ring (already mapped and mx_ring_init'ed by the host
 * runtime), its badge, and the right bits of its render capability.
 * Returns the client index or -1. */
int  mx_render_attach(mx_render_service *s, mx_ring *ring, uint64_t badge, uint32_t rights);
/* Revoke: free the client's surfaces and canvas, mark the ring revoked. */
void mx_render_detach(mx_render_service *s, int client);

/* Policy fills the client's asset table. IMAGE: the store asset must be an
 * IMAGE; its geometry goes into the rcs asset entry now, its pixels when mip
 * `mip` becomes resident. MESH: a BLOB of tri_count*28 bytes. Returns an
 * ASSET_* code; the asset is queued with `priority` (0 = now). */
int  mx_render_grant_image(mx_render_service *s, int client, uint32_t slot, const uint8_t manifest_hash[ASSET_HASH_BYTES], uint32_t mip, uint8_t priority);
int  mx_render_grant_mesh (mx_render_service *s, int client, uint32_t slot, const uint8_t manifest_hash[ASSET_HASH_BYTES], uint8_t priority);

/* One service iteration: poll the store, drain every client's ring. Returns the
 * number of batches processed (0 = idle). */
uint32_t mx_render_pump(mx_render_service *s);

/* Deterministic hash of the client's display canvas ghost (what the sink has seen). */
uint64_t mx_render_display_hash(const mx_render_service *s, int client);

uint64_t mx_now_ns(void);
#endif
