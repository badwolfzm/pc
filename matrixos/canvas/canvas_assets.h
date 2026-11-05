/* canvas_assets.h — glue between the ghost canvas and the asset store.
 * Pure functions take an already-mapped buffer so they can be unit tested
 * without a store on disk; the two thin wrappers at the bottom call
 * asset_map()/asset_poll() and are compiled only with -DGC_HAVE_ASSET_STORE. */
#ifndef CANVAS_ASSETS_H
#define CANVAS_ASSETS_H
#include "ghost_canvas.h"

#define GC_ASSET_NOT_READY 1
#define GC_ASSET_BAD_GEOMETRY (-1)

/* Blit the source rectangle (sx,sy,sw,sh) of a mapped ARGB8888 image with
 * `stride_px` onto the canvas at (dx,dy). Rejects a source rect outside the
 * image (the RCS layer should already have checked RCS_E_RECT_OUTSIDE). */
int gc_blit_image(gc_canvas *c, int dx, int dy, const uint32_t *img, uint32_t img_w, uint32_t img_h,
                  uint32_t stride_px, int sx, int sy, int sw, int sh);

/* Restore the background under a rectangle from a 16x16-tiled background
 * (assetpack --tiles layout: tile row-major, 256 pixels per tile, mip 0 only).
 * Copies whole tiles that intersect the rect; marks them via gc_damage. */
int gc_restore_tiled_bg(gc_canvas *c, const uint32_t *tiles, uint32_t bg_w, uint32_t bg_h, int x, int y, int w, int h);

/* A background binding: which asset is drawn under everything on this canvas. */
typedef struct { uint32_t asset; uint32_t generation; bool bound; } gc_bg_binding;

/* Given an asset event (kind, asset, mip, generation) decide whether the whole
 * canvas must be re-restored: true when the bound background's bytes changed
 * or it was evicted. Updates the stored generation. */
bool gc_bg_event_invalidates(gc_bg_binding *b, uint32_t ev_asset, uint32_t ev_mip, uint32_t ev_generation, bool evicted);

#ifdef GC_HAVE_ASSET_STORE
#include "../assets/asset_store.h"
/* ASSET_OK: blitted. ASSET_NOT_READY: nothing drawn, caller reports it to the
 * client (contract: a blit on a non-resident asset fails synchronously). */
int gc_blit_asset(gc_canvas *c, asset_store *s, asset_handle h, uint32_t mip, int dx, int dy, int sx, int sy, int sw, int sh);
/* Call once per frame outside BEGIN_FRAME..PRESENT. Drains asset_poll and
 * damages the whole canvas if the bound background changed. Returns events seen. */
size_t gc_assets_poll(gc_canvas *c, asset_store *s, gc_bg_binding *bg);
#endif
#endif
