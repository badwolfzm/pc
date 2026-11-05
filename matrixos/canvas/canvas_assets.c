#include "canvas_assets.h"
#include <string.h>

int gc_blit_image(gc_canvas *c, int dx, int dy, const uint32_t *img, uint32_t img_w, uint32_t img_h,
                  uint32_t stride_px, int sx, int sy, int sw, int sh) {
    if (!img || sw <= 0 || sh <= 0 || sx < 0 || sy < 0) return GC_ASSET_BAD_GEOMETRY;
    if ((uint64_t)sx + (uint64_t)sw > img_w || (uint64_t)sy + (uint64_t)sh > img_h || stride_px < img_w) return GC_ASSET_BAD_GEOMETRY;
    gc_blit(c, dx, dy, img + (size_t)sy * stride_px + (size_t)sx, sw, sh, (int)stride_px);
    return 0;
}

int gc_restore_tiled_bg(gc_canvas *c, const uint32_t *tiles, uint32_t bg_w, uint32_t bg_h, int x, int y, int w, int h) {
    if (!tiles || bg_w != c->w || bg_h != c->h) return GC_ASSET_BAD_GEOMETRY;
    if (w <= 0 || h <= 0) return 0;
    int x1 = x + w - 1, y1 = y + h - 1;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 >= (int)c->w) x1 = (int)c->w - 1;
    if (y1 >= (int)c->h) y1 = (int)c->h - 1;
    if (x > x1 || y > y1) return 0;
    uint32_t tw = (bg_w + GC_TILE - 1) / GC_TILE;
    for (uint32_t ty = (uint32_t)y >> GC_TILE_SHIFT; ty <= ((uint32_t)y1 >> GC_TILE_SHIFT); ++ty) {
        uint32_t py0 = ty << GC_TILE_SHIFT, py1 = py0 + GC_TILE; if (py1 > c->h) py1 = c->h;
        for (uint32_t tx = (uint32_t)x >> GC_TILE_SHIFT; tx <= ((uint32_t)x1 >> GC_TILE_SHIFT); ++tx) {
            uint32_t px0 = tx << GC_TILE_SHIFT, px1 = px0 + GC_TILE; if (px1 > c->w) px1 = c->w;
            const uint32_t *tile = tiles + ((size_t)ty * tw + tx) * GC_TILE * GC_TILE;   /* 1 KiB per tile, always full-size in the pack */
            for (uint32_t py = py0; py < py1; ++py)
                memcpy(c->work + (size_t)py * c->w + px0, tile + (size_t)(py - py0) * GC_TILE, (size_t)(px1 - px0) * 4);
            gc_mark_tile(c, tx, ty);
        }
    }
    return 0;
}

bool gc_bg_event_invalidates(gc_bg_binding *b, uint32_t ev_asset, uint32_t ev_mip, uint32_t ev_generation, bool evicted) {
    if (!b->bound || ev_asset != b->asset || ev_mip != 0) return false;
    if (evicted) { b->bound = false; return true; }
    if (ev_generation == b->generation) return false;
    b->generation = ev_generation;
    return true;
}

#ifdef GC_HAVE_ASSET_STORE
int gc_blit_asset(gc_canvas *c, asset_store *s, asset_handle h, uint32_t mip, int dx, int dy, int sx, int sy, int sw, int sh) {
    asset_status st; int rc = asset_query(s, h, &st);
    if (rc != ASSET_OK) return rc;
    if (st.kind != ASSET_KIND_IMAGE || mip >= st.mips) return GC_ASSET_BAD_GEOMETRY;
    const void *data; size_t len;
    rc = asset_map(s, h, mip, &data, &len);
    if (rc != ASSET_OK) return rc;                       /* ASSET_NOT_READY: never blocks */
    uint32_t w = st.width >> mip, hgt = st.height >> mip, stride = st.stride_px >> mip;
    if (!w || !hgt || stride < w || (size_t)stride * hgt * 4 > len) return GC_ASSET_BAD_GEOMETRY;
    return gc_blit_image(c, dx, dy, (const uint32_t *)data, w, hgt, stride, sx, sy, sw, sh);
}

size_t gc_assets_poll(gc_canvas *c, asset_store *s, gc_bg_binding *bg) {
    asset_event ev[64]; size_t total = 0, n;
    bool inval = false;
    while ((n = asset_poll(s, ev, 64)) > 0) {
        total += n;
        for (size_t i = 0; i < n; ++i)
            if (bg && gc_bg_event_invalidates(bg, ev[i].asset, ev[i].mip, ev[i].generation, ev[i].kind == ASSET_EV_EVICTED)) inval = true;
        if (n < 64) break;
    }
    if (inval) gc_damage(c, 0, 0, (int)c->w, (int)c->h);
    return total;
}
#endif
