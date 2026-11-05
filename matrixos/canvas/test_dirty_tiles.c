#define _POSIX_C_SOURCE 199309L
/* Dirty-tile edge tests for ghost_canvas. Complements test_ghost_canvas.c, which
 * covers the end-to-end fuzz, lost update, A->B->A and extreme coordinates.
 * This file pins the bitmap mechanics: word-boundary crossings in the dirty
 * bitmap, more than 64 tile rows (second row-summary word), tile-edge pixels,
 * forced re-send, sink abort bookkeeping, and size validation. */
#include "ghost_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Records every tile the sink receives, so tests can assert the exact set. */
typedef struct { uint32_t n; uint32_t tx[4096], ty[4096]; uint32_t w[4096], h[4096]; } rec;
static int rec_sink(void *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)px; (void)stride;
    rec *r = (rec *)u;
    if (r->n < 4096) { r->tx[r->n] = x >> 4; r->ty[r->n] = y >> 4; r->w[r->n] = w; r->h[r->n] = h; }
    r->n++;
    return 0;
}
static int failing_sink(void *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)x; (void)y; (void)w; (void)h; (void)px; (void)stride;
    ++*(uint32_t *)u;
    return 3;
}

/* Sets the dirty bit for tile (tx,ty) by drawing one pixel at its origin. */
static void touch_tile(gc_canvas *c, uint32_t tx, uint32_t ty, uint32_t color) {
    gc_pset(c, (int)(tx * 16), (int)(ty * 16), color);
}

/* Checks that the set of tiles the sink saw is exactly the expected list. */
static int same_set(const rec *r, const uint32_t *ex, const uint32_t *ey, uint32_t n) {
    if (r->n != n) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        int found = 0;
        for (uint32_t j = 0; j < r->n && j < 4096; ++j)
            if (r->tx[j] == ex[i] && r->ty[j] == ey[i]) { found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

/* Bitmap word boundaries: on a 1280-wide canvas each tile row is 80 bits, so a
 * row's bits straddle the 64-bit word edge at bit 64 (tile x=64 in row 0). The
 * present walk must emit exactly the flagged tiles on both sides of that edge. */
static void test_bitmap_word_boundary(void) {
    gc_canvas c; CHECK(gc_init(&c, 1280, 720) == 0); gc_present(&c, NULL, NULL);
    /* row 0: tiles 62..66 (crosses bit 64); row 1: tile 79 (last in row, bit 159 = word 2) */
    for (uint32_t tx = 62; tx <= 66; ++tx) touch_tile(&c, tx, 0, 0x101010u * (tx + 1));
    touch_tile(&c, 79, 1, 0xFFFFFFu);
    rec r; memset(&r, 0, sizeof r);
    CHECK(gc_present(&c, rec_sink, &r) == 0);
    uint32_t ex[] = {62, 63, 64, 65, 66, 79}, ey[] = {0, 0, 0, 0, 0, 1};
    CHECK(c.last.tiles_dirty == 6 && c.last.tiles_emitted == 6 && c.last.tiles_false == 0);
    CHECK(same_set(&r, ex, ey, 6));
    CHECK(memcmp(c.ghost, c.work, (size_t)1280 * 720 * 4) == 0);
    CHECK(c.dirty[0] == 0 && c.dirty[1] == 0 && c.dirty[2] == 0);   /* every bit consumed */
    /* a second present must not resend anything from the same words */
    gc_present(&c, NULL, NULL); CHECK(c.last.tiles_dirty == 0);
    gc_free(&c);
}

/* More than 64 tile rows: h = 1100 gives th = 69, so the row summary needs a
 * second 64-bit word. Dirty rows 0, 63, 64 and 68 must all be walked, and the
 * partial last tile row (1100 - 68*16 = 12 px) must be clipped. */
static void test_row_summary_second_word(void) {
    gc_canvas c; CHECK(gc_init(&c, 64, 1100) == 0);
    CHECK(c.th == 69 && c.tw == 4);
    gc_present(&c, NULL, NULL);
    touch_tile(&c, 0, 0, 1);
    touch_tile(&c, 3, 63, 2);
    touch_tile(&c, 1, 64, 3);
    touch_tile(&c, 2, 68, 4);                       /* partial tile row: 12 px tall */
    rec r; memset(&r, 0, sizeof r);
    CHECK(gc_present(&c, rec_sink, &r) == 0);
    CHECK(c.last.tiles_emitted == 4 && r.n == 4);
    uint32_t ex[] = {0, 3, 1, 2}, ey[] = {0, 63, 64, 68};
    CHECK(same_set(&r, ex, ey, 4));
    /* the partial last row carries 64 px wide x 12 px high = 768 px */
    int found_partial = 0;
    for (uint32_t j = 0; j < r.n; ++j) if (r.ty[j] == 68) found_partial = (r.h[j] == 12 && r.w[j] == 16);
    CHECK(found_partial);
    CHECK(c.rowsum[0] == 0 && c.rowsum[1] == 0);     /* summaries cleared */
    CHECK(c.ghost[(size_t)1088 * 64 + 32] == 4);     /* tile (2,68) pixel at (32,1088) reached ghost */
    CHECK(c.ghost[(size_t)1024 * 64 + 16] == 3);     /* tile (1,64) */
    gc_free(&c);
}

/* Pixels on tile edges: (15,15) and (16,16) must dirty different tiles; a pixel
 * at (16,0) must not dirty tile 0. A damage rect that ends exactly on a tile
 * border must not spill into the next tile. */
static void test_tile_edges(void) {
    gc_canvas c; CHECK(gc_init(&c, 64, 64) == 0); gc_present(&c, NULL, NULL);
    gc_pset(&c, 15, 15, 1);
    CHECK(c.dirty[0] == 1ull << 0);                  /* only tile (0,0) */
    gc_present(&c, NULL, NULL);
    gc_pset(&c, 16, 16, 1);
    CHECK(c.dirty[0] == 1ull << 5);                  /* tile (1,1) = bit 1*4+1 = 5 */
    gc_present(&c, NULL, NULL);
    gc_damage(&c, 0, 0, 16, 16);                     /* exactly tile (0,0) */
    CHECK(c.dirty[0] == 1ull);
    gc_present(&c, NULL, NULL);
    gc_damage(&c, 0, 0, 17, 1);                      /* one px into tile (1,0) */
    CHECK(c.dirty[0] == 0x3ull);
    gc_present(&c, NULL, NULL);
    gc_damage(&c, 0, 0, 0, 5); gc_damage(&c, 0, 0, 5, 0);   /* zero-size rects mark nothing */
    CHECK(c.dirty[0] == 0 && c.rowsum[0] == 0);
    gc_free(&c);
}

/* gc_pset ignores coordinates outside the canvas and does not dirty a tile for
 * a same-colour write. */
static void test_pset_clip_and_same_colour(void) {
    gc_canvas c; CHECK(gc_init(&c, 32, 32) == 0); gc_present(&c, NULL, NULL);
    gc_pset(&c, -1, 0, 9); gc_pset(&c, 0, -1, 9); gc_pset(&c, 32, 0, 9); gc_pset(&c, 0, 32, 9);
    CHECK(c.dirty[0] == 0 && c.rowsum[0] == 0);
    gc_pset(&c, 3, 3, 0);                            /* already 0: no change, no tile */
    CHECK(c.dirty[0] == 0);
    gc_pset(&c, 3, 3, 5);
    CHECK(c.dirty[0] == 1ull);
    gc_free(&c);
}

/* gc_invalidate_all sends every tile on the next present, even unchanged ones,
 * and the force flag resets afterwards. */
static void test_invalidate_all_forces_resend(void) {
    gc_canvas c; CHECK(gc_init(&c, 480, 270) == 0);
    gc_present(&c, NULL, NULL);                      /* first present clears init force */
    CHECK(!c.force);
    gc_invalidate_all(&c);
    CHECK(c.force);
    rec r; memset(&r, 0, sizeof r);
    gc_present(&c, rec_sink, &r);
    CHECK(c.last.tiles_emitted == 510 && c.last.tiles_false == 0);
    CHECK(r.n == 510);
    CHECK(!c.force);
    gc_present(&c, NULL, NULL); CHECK(c.last.tiles_emitted == 0);
    gc_free(&c);
}

/* gc_damage_bits: NULL and mismatched geometry are rejected without touching
 * the bitmap; an all-zero bitmap imports nothing; a bitmap with only the last
 * partial tile sets exactly one row summary bit. */
static void test_damage_bits_edges(void) {
    gc_canvas c; CHECK(gc_init(&c, 37, 19) == 0);   /* tw=3, th=2 */
    gc_present(&c, NULL, NULL);
    uint8_t bits[2] = {0, 0};
    CHECK(gc_damage_bits(&c, NULL, 3, 2) == -1);
    CHECK(gc_damage_bits(&c, bits, 3, 3) == -1);
    CHECK(gc_damage_bits(&c, bits, 2, 2) == -1);
    CHECK(gc_damage_bits(&c, bits, 3, 2) == 0);
    CHECK(c.dirty[0] == 0 && c.rowsum[0] == 0);
    bits[0] = 0;
    uint32_t last = 1 * 3 + 2;                       /* tile (2,1): partial in both axes */
    bits[last >> 3] |= (uint8_t)(1u << (last & 7));
    CHECK(gc_damage_bits(&c, bits, 3, 2) == 1);
    CHECK(c.rowsum[0] == 2u);                        /* only tile row 1 */
    rec r; memset(&r, 0, sizeof r);
    gc_present(&c, rec_sink, &r);
    CHECK(c.last.tiles_emitted == 0 && c.last.tiles_false == 1);   /* no pixel changed: XOR drops it */
    gc_damage_bits(&c, bits, 3, 2);
    c.work[17 * 37 + 34] = 0x00AA55u;                /* kernel wrote a pixel in tile (2,1) */
    gc_present(&c, rec_sink, &r);
    CHECK(c.last.tiles_emitted == 1 && r.n == 1);
    CHECK(r.tx[0] == 2 && r.ty[0] == 1 && r.w[0] == 5 && r.h[0] == 3);  /* 37-32=5, 19-16=3 */
    gc_free(&c);
}

/* A sink abort still clears every dirty bit and advances the frame counter. The
 * first failing return is passed back; later dirty tiles are still reconciled
 * into the ghost so the next frame starts clean. */
static void test_sink_abort_clears_bits(void) {
    gc_canvas c; CHECK(gc_init(&c, 64, 64) == 0); gc_present(&c, NULL, NULL);
    uint64_t f0 = c.frame;
    touch_tile(&c, 0, 0, 1); touch_tile(&c, 1, 1, 2); touch_tile(&c, 3, 2, 3);
    uint32_t calls = 0;
    CHECK(gc_present(&c, failing_sink, &calls) == 3);
    CHECK(calls == 1);                               /* sink not called again after the abort */
    CHECK(c.frame == f0 + 1);                        /* one present call since f0 */
    CHECK(c.dirty[0] == 0 && c.rowsum[0] == 0);
    CHECK(c.last.tiles_emitted == 3);
    CHECK(memcmp(c.ghost, c.work, (size_t)64 * 64 * 4) == 0);   /* ghost still tracks work */
    gc_free(&c);
}

/* Size validation and bitmap sizing. Values match the README cost table. */
static void test_init_validation_and_bitmap_bytes(void) {
    gc_canvas c;
    CHECK(gc_init(&c, 0, 10) == -1);
    CHECK(gc_init(&c, 10, 0) == -1);
    CHECK(gc_init(&c, 16385, 16) == -1);
    CHECK(gc_init(&c, 16, 16385) == -1);
    CHECK(gc_bitmap_bytes(480, 270) == 72);
    CHECK(gc_bitmap_bytes(1280, 720) == 464);
    CHECK(gc_bitmap_bytes(16, 16) == 16);
    CHECK(gc_init(&c, 1, 1) == 0);
    CHECK(c.tw == 1 && c.th == 1);
    gc_pset(&c, 0, 0, 7);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.pixels_emitted == 1 && c.ghost[0] == 7);
    gc_free(&c);
    CHECK(c.work == NULL && c.dirty == NULL);        /* gc_free leaves no dangling pointers */
}

int main(void) {
    test_bitmap_word_boundary();
    test_row_summary_second_word();
    test_tile_edges();
    test_pset_clip_and_same_colour();
    test_invalidate_all_forces_resend();
    test_damage_bits_edges();
    test_sink_abort_clears_bits();
    test_init_validation_and_bitmap_bytes();
    printf("dirty-tile: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
