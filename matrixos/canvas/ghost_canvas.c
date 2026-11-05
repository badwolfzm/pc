#include "ghost_canvas.h"
#include <stdlib.h>
#include <string.h>

static uint32_t ceil_div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

size_t gc_bitmap_bytes(uint32_t w, uint32_t h) {
    uint32_t tw = ceil_div(w, GC_TILE), th = ceil_div(h, GC_TILE);
    return (size_t)ceil_div(tw * th, 64) * 8 + (size_t)ceil_div(th, 64) * 8;
}

int gc_init(gc_canvas *c, uint32_t w, uint32_t h) {
    memset(c, 0, sizeof *c);
    if (!w || !h || w > 16384 || h > 16384) return -1;
    c->w = w; c->h = h;
    c->tw = ceil_div(w, GC_TILE); c->th = ceil_div(h, GC_TILE);
    size_t n = (size_t)w * h;
    c->work  = (uint32_t *)calloc(n, 4);
    c->ghost = (uint32_t *)calloc(n, 4);
    c->dirty  = (uint64_t *)calloc(ceil_div(c->tw * c->th, 64), 8);
    c->rowsum = (uint64_t *)calloc(ceil_div(c->th, 64), 8);
    if (!c->work || !c->ghost || !c->dirty || !c->rowsum) { gc_free(c); return -1; }
    gc_invalidate_all(c);          /* first present sends the whole frame */
    return 0;
}

void gc_free(gc_canvas *c) {
    free(c->work); free(c->ghost); free(c->dirty); free(c->rowsum);
    memset(c, 0, sizeof *c);
}

void gc_damage(gc_canvas *c, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w - 1, y1 = y + h - 1;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 >= (int)c->w) x1 = (int)c->w - 1;
    if (y1 >= (int)c->h) y1 = (int)c->h - 1;
    if (x > x1 || y > y1) return;
    uint32_t tx0 = (uint32_t)x >> GC_TILE_SHIFT, tx1 = (uint32_t)x1 >> GC_TILE_SHIFT;
    uint32_t ty0 = (uint32_t)y >> GC_TILE_SHIFT, ty1 = (uint32_t)y1 >> GC_TILE_SHIFT;
    for (uint32_t ty = ty0; ty <= ty1; ++ty)
        for (uint32_t tx = tx0; tx <= tx1; ++tx) gc_mark_tile(c, tx, ty);
}

void gc_invalidate_all(gc_canvas *c) { gc_damage(c, 0, 0, (int)c->w, (int)c->h); c->force = true; }

void gc_clear(gc_canvas *c, uint32_t color) {
    /* Tile-wise so that an unchanged region stays clean. */
    for (uint32_t ty = 0; ty < c->th; ++ty) {
        uint32_t y0 = ty << GC_TILE_SHIFT, y1 = y0 + GC_TILE; if (y1 > c->h) y1 = c->h;
        for (uint32_t tx = 0; tx < c->tw; ++tx) {
            uint32_t x0 = tx << GC_TILE_SHIFT, x1 = x0 + GC_TILE; if (x1 > c->w) x1 = c->w;
            bool changed = false;
            for (uint32_t y = y0; y < y1; ++y) {
                uint32_t *row = c->work + (size_t)y * c->w;
                for (uint32_t x = x0; x < x1; ++x)
                    if (row[x] != color) { row[x] = color; changed = true; }
            }
            if (changed) gc_mark_tile(c, tx, ty);
        }
    }
}

void gc_rect(gc_canvas *c, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > (int)c->w) x1 = (int)c->w;
    if (y1 > (int)c->h) y1 = (int)c->h;
    for (int j = y; j < y1; ++j) {
        uint32_t *row = c->work + (size_t)j * c->w;
        for (int i = x; i < x1; ++i) {
            if (row[i] != color) { row[i] = color;
                gc_mark_tile(c, (uint32_t)i >> GC_TILE_SHIFT, (uint32_t)j >> GC_TILE_SHIFT); }
        }
    }
}

void gc_line(gc_canvas *c, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gc_pset(c, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gc_blit(gc_canvas *c, int x, int y, const uint32_t *src, int w, int h, int stride) {
    if (w <= 0 || h <= 0) return;
    int sx = 0, sy = 0;
    if (x < 0) { sx = -x; x = 0; }
    if (y < 0) { sy = -y; y = 0; }
    int cw = w - sx, ch = h - sy;
    if (x + cw > (int)c->w) cw = (int)c->w - x;
    if (y + ch > (int)c->h) ch = (int)c->h - y;
    if (cw <= 0 || ch <= 0) return;
    for (int j = 0; j < ch; ++j) {
        const uint32_t *s = src + (size_t)(sy + j) * stride + sx;
        uint32_t *d = c->work + (size_t)(y + j) * c->w + x;
        if (memcmp(d, s, (size_t)cw * 4) == 0) continue;   /* whole row unchanged */
        memcpy(d, s, (size_t)cw * 4);
        gc_damage(c, x, y + j, cw, 1);
    }
}

int gc_present(gc_canvas *c, gc_sink_fn sink, void *user) {
    gc_stats st = {0};
    int rc = 0;
    for (uint32_t rw = 0; rw < ceil_div(c->th, 64); ++rw) {
        uint64_t rows = c->rowsum[rw];
        c->rowsum[rw] = 0;
        while (rows) {
            uint32_t ty = rw * 64 + (uint32_t)__builtin_ctzll(rows);
            rows &= rows - 1;
            uint32_t y0 = ty << GC_TILE_SHIFT, y1 = y0 + GC_TILE; if (y1 > c->h) y1 = c->h;
            uint32_t base = ty * c->tw;
            /* walk the dirty bits of this tile row */
            uint32_t b = base, bend = base + c->tw;
            while (b < bend) {
                uint32_t wi = b >> 6, lo = b & 63;
                uint64_t word = c->dirty[wi] >> lo;
                uint32_t span = 64 - lo; if (b + span > bend) span = bend - b;
                if (span < 64) word &= ((uint64_t)1 << span) - 1;
                /* clear the bits we consume */
                c->dirty[wi] &= ~(((span < 64) ? (((uint64_t)1 << span) - 1) : ~(uint64_t)0) << lo);
                while (word) {
                    uint32_t tx = (b - base) + (uint32_t)__builtin_ctzll(word);
                    word &= word - 1;
                    st.tiles_dirty++;
                    uint32_t x0 = tx << GC_TILE_SHIFT, x1 = x0 + GC_TILE; if (x1 > c->w) x1 = c->w;
                    uint32_t tw = x1 - x0;
                    /* XOR compare work vs ghost; copy on first difference */
                    bool diff = c->force;
                    for (uint32_t y = y0; y < y1 && !diff; ++y) {
                        const uint32_t *wr = c->work + (size_t)y * c->w + x0;
                        uint32_t *gr = c->ghost + (size_t)y * c->w + x0;
                        uint32_t acc = 0;
                        for (uint32_t i = 0; i < tw; ++i) acc |= wr[i] ^ gr[i];
                        if (acc) { diff = true; break; }
                    }
                    if (!diff) { st.tiles_false++; continue; }
                    for (uint32_t y = y0; y < y1; ++y)
                        memcpy(c->ghost + (size_t)y * c->w + x0, c->work + (size_t)y * c->w + x0, (size_t)tw * 4);
                    st.tiles_emitted++;
                    st.pixels_emitted += (uint64_t)tw * (y1 - y0);
                    if (sink && !rc)
                        rc = sink(user, x0, y0, tw, y1 - y0, c->ghost + (size_t)y0 * c->w + x0, c->w);
                }
                b += span;
            }
        }
    }
    st.bytes_emitted = st.pixels_emitted * 4;
    c->force = false;
    c->last = st;
    c->frame++;
    return rc;
}

uint64_t gc_ghost_hash(const gc_canvas *c) {
    uint64_t h = 1469598103934665603ULL;
    const uint8_t *p = (const uint8_t *)c->ghost;
    size_t n = (size_t)c->w * c->h * 4;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
