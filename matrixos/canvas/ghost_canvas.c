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

/* ---- clipping helpers: all intermediate arithmetic in int64 so INT_MIN/INT_MAX
 * inputs cannot overflow (audit 2026-11-05, UBSan probes). A rect is clipped to
 * the canvas BEFORE any loop runs, so iteration counts are bounded by w*h. ---- */
typedef struct { int64_t x0, y0, x1, y1; } gc_span;   /* inclusive, clipped, valid when x0 <= x1 && y0 <= y1 */

static bool gc_clip_rect(const gc_canvas *c, int64_t x, int64_t y, int64_t w, int64_t h, gc_span *s) {
    if (w <= 0 || h <= 0) return false;
    int64_t x1 = x + w - 1, y1 = y + h - 1;       /* |x|,|w| < 2^31, so no overflow in int64 */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int64_t)c->w - 1) x1 = (int64_t)c->w - 1;
    if (y1 > (int64_t)c->h - 1) y1 = (int64_t)c->h - 1;
    if (x > x1 || y > y1) return false;
    s->x0 = x; s->y0 = y; s->x1 = x1; s->y1 = y1;
    return true;
}

void gc_damage(gc_canvas *c, int x, int y, int w, int h) {
    gc_span s;
    if (!gc_clip_rect(c, x, y, w, h, &s)) return;
    uint32_t tx0 = (uint32_t)s.x0 >> GC_TILE_SHIFT, tx1 = (uint32_t)s.x1 >> GC_TILE_SHIFT;
    uint32_t ty0 = (uint32_t)s.y0 >> GC_TILE_SHIFT, ty1 = (uint32_t)s.y1 >> GC_TILE_SHIFT;
    for (uint32_t ty = ty0; ty <= ty1; ++ty)
        for (uint32_t tx = tx0; tx <= tx1; ++tx) gc_mark_tile(c, tx, ty);
}

int gc_damage_bits(gc_canvas *c, const uint8_t *tiles, uint32_t tw, uint32_t th) {
    if (!tiles || tw != c->tw || th != c->th) return -1;
    int n = 0;
    uint32_t total = tw * th;
    for (uint32_t ty = 0; ty < th; ++ty) {
        bool any = false;
        for (uint32_t tx = 0; tx < tw; ++tx) {
            uint32_t bit = ty * tw + tx;
            if (tiles[bit >> 3] & (1u << (bit & 7))) { c->dirty[bit >> 6] |= (uint64_t)1 << (bit & 63); any = true; ++n; }
        }
        if (any) c->rowsum[ty >> 6] |= (uint64_t)1 << (ty & 63);
    }
    (void)total;
    return n;
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
    gc_span s;
    if (!gc_clip_rect(c, x, y, w, h, &s)) return;
    for (int64_t j = s.y0; j <= s.y1; ++j) {
        uint32_t *row = c->work + (size_t)j * c->w;
        for (int64_t i = s.x0; i <= s.x1; ++i) {
            if (row[i] != color) { row[i] = color;
                gc_mark_tile(c, (uint32_t)i >> GC_TILE_SHIFT, (uint32_t)j >> GC_TILE_SHIFT); }
        }
    }
}

/* Bounded, exact line rasterizer. Bresenham (Zingl variant) is a linear
 * recurrence, so the state after k iterations has a closed form: for an x-major
 * line with a = |dx| >= b = |dy|, after k iterations the minor axis has moved
 * n = (2*k*b + a) / (2*a) steps and err = a - b - k*b + n*a. We jump straight
 * to the first iteration whose major coordinate is inside the canvas and then
 * run at most (canvas extent along the major axis) iterations. Pixels are
 * identical to the unclipped rasterization (test: 200 random lines against a
 * brute-force reference, exact match), and INT_MIN..INT_MAX endpoints cost a
 * few hundred steps instead of 4 billion. All arithmetic is 64/128-bit. */
static void gc_line_major(gc_canvas *c, int64_t maj0, int64_t min0, int64_t a, int64_t b, int64_t smaj, int64_t smin,
                          int64_t maj_lo, int64_t maj_hi, bool x_major, uint32_t color) {
    /* iterations until the major coordinate enters [maj_lo, maj_hi] */
    int64_t k = 0;
    if (maj0 < maj_lo)      { if (smaj < 0) return; k = maj_lo - maj0; }
    else if (maj0 > maj_hi) { if (smaj > 0) return; k = maj0 - maj_hi; }
    if (k > a) return;                                  /* the segment ends before reaching the canvas */
    int64_t n = (int64_t)(((__int128)2 * k * b + a) / ((__int128)2 * a));
    int64_t err = (int64_t)((__int128)a - b - (__int128)k * b + (__int128)n * a);
    int64_t maj = maj0 + k * smaj, mn = min0 + n * smin;
    int64_t remaining = a - k;                          /* iterations left to the true endpoint */
    int64_t span = maj_hi - maj_lo + 1;                 /* at most this many major steps are visible */
    int64_t iters = remaining < span ? remaining : span;
    for (int64_t i = 0; ; ++i) {
        int64_t x = x_major ? maj : mn, y = x_major ? mn : maj;
        if (x >= 0 && y >= 0 && x < (int64_t)c->w && y < (int64_t)c->h) gc_pset(c, (int)x, (int)y, color);
        if (i == iters) break;
        /* one Zingl step with a >= b: the major axis always advances */
        int64_t e2 = 2 * err;
        err -= b; maj += smaj;
        if (e2 <= a) { err += a; mn += smin; }
    }
}

void gc_line(gc_canvas *c, int ix0, int iy0, int ix1, int iy1, uint32_t color) {
    int64_t x0 = ix0, y0 = iy0, x1 = ix1, y1 = iy1;
    int64_t dx = x1 - x0, dy = y1 - y0;                 /* |coords| <= 2^31 so these fit in int64 */
    int64_t a = dx < 0 ? -dx : dx, b = dy < 0 ? -dy : dy;
    int64_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    if (a == 0 && b == 0) { if (x0 >= 0 && y0 >= 0 && x0 < (int64_t)c->w && y0 < (int64_t)c->h) gc_pset(c, (int)x0, (int)y0, color); return; }
    if (a >= b) gc_line_major(c, x0, y0, a, b, sx, sy, 0, (int64_t)c->w - 1, true, color);
    else        gc_line_major(c, y0, x0, b, a, sy, sx, 0, (int64_t)c->h - 1, false, color);
}

void gc_blit(gc_canvas *c, int ix, int iy, const uint32_t *src, int w, int h, int stride) {
    if (!src || w <= 0 || h <= 0 || stride < w) return;
    gc_span s;
    if (!gc_clip_rect(c, ix, iy, w, h, &s)) return;
    int64_t sx = s.x0 - ix, sy = s.y0 - iy;          /* source offset of the clipped origin, >= 0 */
    int64_t cw = s.x1 - s.x0 + 1, ch = s.y1 - s.y0 + 1;
    for (int64_t j = 0; j < ch; ++j) {
        const uint32_t *sp = src + (size_t)(sy + j) * (size_t)stride + (size_t)sx;
        uint32_t *d = c->work + (size_t)(s.y0 + j) * c->w + (size_t)s.x0;
        if (memcmp(d, sp, (size_t)cw * 4) == 0) continue;   /* whole row unchanged */
        memcpy(d, sp, (size_t)cw * 4);
        gc_damage(c, (int)s.x0, (int)(s.y0 + j), (int)cw, 1);
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
