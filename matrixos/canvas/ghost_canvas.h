/* ghost_canvas.h — tile dirty-bit ghost canvas for the Warden render service.
 *
 * Two buffers: `work` (what the game draws into) and `ghost` (what the
 * encoder/streamer last received).  Drawing sets a 1-bit flag per TILE
 * (default 16x16 pixels) when a pixel is written with a different value.
 * present() walks only the dirty tiles, XOR-compares each against the
 * ghost, copies the tile into the ghost, hands the tile to the sink
 * (encoder) and clears the flag.  Nothing per-frame touches a clean tile.
 *
 * Why tiles and not the per-pixel XOR toggle in matrixos_v9.c:
 *   - the toggle flips twice on A->B->C and reads "clean" (lost update);
 *   - a per-pixel bitmap is 1/32 of the frame and must be scanned fully;
 *   - encoders work in 16x16 macroblocks anyway, so a tile is the
 *     natural damage unit for the streaming stage.
 *
 * Portable C11, integer only, no allocation after init.  Single thread:
 * the render service owns the canvas; the game talks to it over IPC.
 */
#ifndef GHOST_CANVAS_H
#define GHOST_CANVAS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef GC_TILE
#define GC_TILE 16            /* tile edge in pixels; must be a power of two */
#endif
#ifndef GC_TILE_SHIFT
#define GC_TILE_SHIFT 4
#endif /*      log2(GC_TILE) — keep in sync */

typedef struct gc_canvas gc_canvas;

/* Called once per dirty tile at present().  x,y are pixel coords of the
 * tile origin; w,h the clipped tile size; px points into the ghost (already
 * updated) with stride `stride` in pixels.  Return nonzero to abort. */
typedef int (*gc_sink_fn)(void *user, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const uint32_t *px, uint32_t stride);

typedef struct {
    uint32_t tiles_dirty;     /* tiles flagged by draws since last present */
    uint32_t tiles_emitted;   /* tiles actually sent (flagged AND differing) */
    uint32_t tiles_false;     /* flagged but XOR said identical (A->B->A) */
    uint64_t pixels_emitted;  /* pixels copied into ghost / sent */
    uint64_t bytes_emitted;   /* pixels_emitted * 4 */
} gc_stats;

struct gc_canvas {
    uint32_t w, h;
    uint32_t tw, th;          /* tiles across / down (ceil) */
    uint32_t words_per_row;   /* 64-bit words per tile row of the bitmap */
    uint32_t *work;           /* w*h RGBA8 (0x00RRGGBB) */
    uint32_t *ghost;          /* w*h, what the sink last saw */
    uint64_t *dirty;          /* tw*th bits, one per tile */
    uint64_t *rowsum;         /* th bits: any tile dirty in that tile row */
    gc_stats last;            /* stats of the last present() */
    uint64_t frame;           /* present() counter */
    bool force;               /* next present sends every dirty tile without XOR test */
};

int  gc_init(gc_canvas *c, uint32_t w, uint32_t h);   /* 0 ok, -1 oom/bad size */
void gc_free(gc_canvas *c);

/* Drawing primitives.  All clip to the canvas and mark tiles only when a
 * pixel value actually changes. */
static inline void gc_mark_tile(gc_canvas *c, uint32_t tx, uint32_t ty) {
    uint32_t bit = ty * c->tw + tx;
    c->dirty[bit >> 6] |= (uint64_t)1 << (bit & 63);
    c->rowsum[ty >> 6] |= (uint64_t)1 << (ty & 63);
}
static inline void gc_pset(gc_canvas *c, int x, int y, uint32_t color) {
    if ((unsigned)x >= c->w || (unsigned)y >= c->h) return;
    uint32_t idx = (uint32_t)y * c->w + (uint32_t)x;
    if (c->work[idx] == color) return;
    c->work[idx] = color;
    gc_mark_tile(c, (uint32_t)x >> GC_TILE_SHIFT, (uint32_t)y >> GC_TILE_SHIFT);
}
void gc_clear(gc_canvas *c, uint32_t color);
void gc_rect(gc_canvas *c, int x, int y, int w, int h, uint32_t color);
void gc_line(gc_canvas *c, int x0, int y0, int x1, int y1, uint32_t color);
/* Opaque blit of a w*h RGBA source with stride (pixels). */
void gc_blit(gc_canvas *c, int x, int y, const uint32_t *src, int w, int h, int stride);
/* Mark a pixel rectangle dirty without testing values (for callers that
 * wrote `work` directly, e.g. a SIMD sprite kernel). */
void gc_damage(gc_canvas *c, int x, int y, int w, int h);

/* Composite: for each dirty tile, XOR-compare work vs ghost; if any pixel
 * differs copy the tile into ghost and call sink.  Clears all dirty bits.
 * Returns 0 or the first nonzero sink return. */
int  gc_present(gc_canvas *c, gc_sink_fn sink, void *user);

/* Force the whole canvas to be re-sent next present (new client, key frame). */
void gc_invalidate_all(gc_canvas *c);

/* Deterministic FNV-1a over the ghost, for golden tests / trace hashing. */
uint64_t gc_ghost_hash(const gc_canvas *c);

/* Byte sizes, for the cost table. */
size_t gc_bitmap_bytes(uint32_t w, uint32_t h);
#endif
