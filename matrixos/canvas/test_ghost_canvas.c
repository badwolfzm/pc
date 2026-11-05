#define _POSIX_C_SOURCE 199309L
/* Tests for ghost_canvas: correctness of damage tracking and streaming. */
#include "ghost_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* A client that reconstructs the frame from the streamed tiles. */
typedef struct { uint32_t *fb; uint32_t w, h; uint64_t tiles, bytes; } client;
static int client_sink(void *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    client *cl = (client *)u;
    for (uint32_t j = 0; j < h; ++j)
        memcpy(cl->fb + (size_t)(y + j) * cl->w + x, px + (size_t)j * stride, (size_t)w * 4);
    cl->tiles++; cl->bytes += (uint64_t)w * h * 4;
    return 0;
}
static int abort_sink(void *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)u;(void)x;(void)y;(void)w;(void)h;(void)px;(void)stride; return 7;
}
static uint64_t rng_s = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { uint64_t x = rng_s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; rng_s = x; return x; }

static void test_first_present_sends_everything(void) {
    gc_canvas c; CHECK(gc_init(&c, 480, 270) == 0);
    CHECK(c.tw == 30 && c.th == 17);
    client cl = { calloc(480 * 270, 4), 480, 270, 0, 0 };
    CHECK(gc_present(&c, client_sink, &cl) == 0);
    CHECK(c.last.tiles_emitted == 30 * 17);
    CHECK(c.last.pixels_emitted == 480 * 270);       /* edge tiles clipped, no overlap */
    CHECK(memcmp(cl.fb, c.ghost, 480 * 270 * 4) == 0);
    /* a second present with nothing drawn sends nothing */
    CHECK(gc_present(&c, client_sink, &cl) == 0);
    CHECK(c.last.tiles_dirty == 0 && c.last.tiles_emitted == 0 && c.last.bytes_emitted == 0);
    free(cl.fb); gc_free(&c);
}

/* The per-pixel XOR toggle in matrixos_v9.c: A->B->C flips the bit twice. */
static void test_old_toggle_loses_abc(void) {
    uint8_t shadow = 0;
    uint32_t px = 0xAA;
    #define OLD_SET(v) do { if (px != (v)) { px = (v); shadow ^= 1; } } while (0)
    OLD_SET(0xBB); OLD_SET(0xCC);
    CHECK(shadow == 0);   /* old scheme reads "clean" although px changed A->C */
    #undef OLD_SET
    /* new scheme */
    gc_canvas c; CHECK(gc_init(&c, 64, 64) == 0);
    gc_present(&c, NULL, NULL);
    gc_pset(&c, 5, 5, 0xAA); gc_present(&c, NULL, NULL);
    gc_pset(&c, 5, 5, 0xBB); gc_pset(&c, 5, 5, 0xCC);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_emitted == 1);
    CHECK(c.ghost[5 * 64 + 5] == 0xCC);
    gc_free(&c);
}

static void test_aba_is_suppressed(void) {
    gc_canvas c; CHECK(gc_init(&c, 64, 64) == 0);
    gc_clear(&c, 0x111111); gc_present(&c, NULL, NULL);
    gc_pset(&c, 20, 20, 0x222222); gc_pset(&c, 20, 20, 0x111111);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_dirty == 1);
    CHECK(c.last.tiles_false == 1);
    CHECK(c.last.tiles_emitted == 0);
    gc_free(&c);
}

static void test_clear_marks_only_changed_tiles(void) {
    gc_canvas c; CHECK(gc_init(&c, 480, 270) == 0);
    gc_clear(&c, 0x101010); gc_present(&c, NULL, NULL);
    gc_clear(&c, 0x101010);                       /* same colour again */
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_dirty == 0);
    gc_rect(&c, 100, 100, 10, 10, 0xFF0000);       /* one tile (96..111, 96..111) */
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_emitted == 1);
    CHECK(c.last.pixels_emitted == 256);
    gc_free(&c);
}

static void test_bounds(void) {
    gc_canvas c; CHECK(gc_init(&c, 100, 50) == 0);
    gc_present(&c, NULL, NULL);
    gc_rect(&c, -1000, -1000, 10, 10, 1);
    gc_rect(&c, 100, 50, 10, 10, 1);
    gc_rect(&c, 0, 0, 0, 0, 1);
    gc_pset(&c, -1, 0, 1); gc_pset(&c, 100, 49, 1);
    gc_line(&c, -50, -50, -10, -10, 1);
    gc_damage(&c, 200, 200, 5, 5);
    gc_damage(&c, 0, 0, -5, -5);
    uint32_t spr[4] = {1,2,3,4};
    gc_blit(&c, 200, 200, spr, 2, 2, 2);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_dirty == 0);
    /* clipped draws that do touch the canvas */
    gc_rect(&c, -5, -5, 10, 10, 2);                /* covers (0..4,0..4) */
    gc_rect(&c, 95, 45, 100, 100, 3);              /* bottom-right corner */
    gc_blit(&c, -1, -1, spr, 2, 2, 2);             /* only (0,0)=4 lands */
    gc_present(&c, NULL, NULL);
    CHECK(c.ghost[0] == 4 && c.ghost[4] == 2 && c.ghost[49 * 100 + 99] == 3);
    /* corner rect spans x tiles 5,6 and y tiles 2,3; plus tile 0 */
    CHECK(c.last.tiles_emitted == 5);
    gc_free(&c);
}

static void test_damage_without_change_is_free(void) {
    gc_canvas c; CHECK(gc_init(&c, 128, 128) == 0);
    gc_present(&c, NULL, NULL);
    gc_damage(&c, 0, 0, 128, 128);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_dirty == 64 && c.last.tiles_emitted == 0 && c.last.tiles_false == 64);
    /* direct write + damage is the SIMD-kernel path */
    c.work[77 * 128 + 33] = 0xABCDEF; gc_damage(&c, 33, 77, 1, 1);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_emitted == 1 && c.ghost[77 * 128 + 33] == 0xABCDEF);
    gc_free(&c);
}

static void test_sink_abort(void) {
    gc_canvas c; CHECK(gc_init(&c, 32, 32) == 0);
    CHECK(gc_present(&c, abort_sink, NULL) == 7);
    gc_free(&c);
}

static void test_fuzz_ghost_tracks_work(uint32_t w, uint32_t h, int frames) {
    gc_canvas c; CHECK(gc_init(&c, w, h) == 0);
    client cl = { calloc((size_t)w * h, 4), w, h, 0, 0 };
    uint32_t spr[64 * 64];
    for (int i = 0; i < 64 * 64; ++i) spr[i] = (uint32_t)rnd();
    for (int f = 0; f < frames; ++f) {
        int nops = 1 + (int)(rnd() % 12);
        for (int k = 0; k < nops; ++k) {
            int x = (int)(rnd() % (w + 80)) - 40, y = (int)(rnd() % (h + 80)) - 40;
            uint32_t col = (uint32_t)rnd() & 0xFFFFFF;
            switch (rnd() % 6) {
            case 0: gc_pset(&c, x, y, col); break;
            case 1: gc_rect(&c, x, y, (int)(rnd() % 90), (int)(rnd() % 90), col); break;
            case 2: gc_line(&c, x, y, (int)(rnd() % w), (int)(rnd() % h), col); break;
            case 3: gc_blit(&c, x, y, spr, 1 + (int)(rnd() % 64), 1 + (int)(rnd() % 64), 64); break;
            case 4: if (rnd() % 10 == 0) gc_clear(&c, col); break;
            case 5: c.work[(size_t)(rnd() % h) * w + rnd() % w] = col; gc_damage(&c, 0, 0, (int)w, (int)h); break;
            }
        }
        CHECK(gc_present(&c, client_sink, &cl) == 0);
        /* invariants after every present */
        if (memcmp(c.ghost, c.work, (size_t)w * h * 4) != 0) { CHECK(!"ghost == work"); break; }
        if (memcmp(cl.fb, c.work, (size_t)w * h * 4) != 0) { CHECK(!"client == work"); break; }
        for (uint32_t i = 0; i < (c.tw * c.th + 63) / 64; ++i) if (c.dirty[i]) { CHECK(!"dirty cleared"); break; }
        if (c.last.tiles_emitted + c.last.tiles_false != c.last.tiles_dirty) { CHECK(!"stats add up"); break; }
    }
    free(cl.fb); gc_free(&c);
}

static void test_deterministic_hash(void) {
    uint64_t hs[2];
    for (int r = 0; r < 2; ++r) {
        gc_canvas c; CHECK(gc_init(&c, 480, 270) == 0);
        for (int f = 0; f < 30; ++f) {
            gc_clear(&c, 0x101010u + (uint32_t)(f & 0x1F));
            gc_rect(&c, (f * 3) % 300, 20, 80, 50, 0xFFAA00);
            gc_line(&c, 0, f % 270, 479, (f * 7) % 270, 0xFFFFFF);
            gc_present(&c, NULL, NULL);
        }
        hs[r] = gc_ghost_hash(&c);
        gc_free(&c);
    }
    CHECK(hs[0] == hs[1]);
    printf("golden ghost hash (480x270, 30 scripted frames): %016llx\n", (unsigned long long)hs[0]);
}

/* Regression for the baseline bench: XORing identical per-iteration values over an
 * even number of iterations cancels to 0, so "output consumed" proved nothing.
 * The fixed mixing (iteration index folded in, whole buffer hashed) must be
 * nonzero, and must change when one pixel changes. */
static uint64_t fnv_mix(uint64_t h, const void *d, size_t n) {
    const uint8_t *p = (const uint8_t *)d; if (!h) h = 1469598103934665603ULL;
    for (size_t k = 0; k < n; ++k) { h ^= p[k]; h *= 1099511628211ULL; } return h;
}
static void test_consumed_hash_does_not_cancel(void) {
    uint64_t old = 0, fixed = 0, fixed2 = 0;
    for (uint64_t i = 0; i < 18; ++i) {
        gc_canvas c; CHECK(gc_init(&c, 480, 270) == 0);
        for (int f = 0; f < 30; ++f) { gc_clear(&c, 0x101010u + (uint32_t)(f & 0x1F)); gc_rect(&c, (f * 3) % 300, 20, 80, 50, 0xFFAA00); gc_present(&c, NULL, NULL); }
        uint64_t changed = c.last.pixels_emitted;
        old ^= changed ^ c.work[12345];                                   /* baseline scheme */
        fixed ^= fnv_mix(fixed ^ (i + 1) * 0x9E3779B97F4A7C15ULL, c.work, 480 * 270 * 4) ^ (changed * (i + 1));
        if (i == 17) c.work[777] ^= 1;                                      /* one pixel differs */
        fixed2 ^= fnv_mix(fixed2 ^ (i + 1) * 0x9E3779B97F4A7C15ULL, c.work, 480 * 270 * 4) ^ (changed * (i + 1));
        gc_free(&c);
    }
    CHECK(old == 0);          /* the baseline's cancellation, reproduced */
    CHECK(fixed != 0);
    CHECK(fixed != fixed2);
}

/* Service hand-off: rcs_present_info.tiles has the same geometry as ours. */
static void test_damage_bits_import(void) {
    gc_canvas c; CHECK(gc_init(&c, 480, 270) == 0); gc_present(&c, NULL, NULL);
    uint8_t bits[(30 * 17 + 7) / 8]; memset(bits, 0, sizeof bits);
    /* service says: tiles (6..10, 12..16) and tile (29,16) (bottom-right, partial) were drawn */
    for (uint32_t ty = 12; ty <= 16; ++ty) for (uint32_t tx = 6; tx <= 10; ++tx) { uint32_t b = ty * 30 + tx; bits[b >> 3] |= (uint8_t)(1u << (b & 7)); }
    { uint32_t b = 16 * 30 + 29; bits[b >> 3] |= (uint8_t)(1u << (b & 7)); }
    /* kernel wrote work directly in those tiles, but only some pixels differ */
    c.work[200 * 480 + 100] = 0xABCDEF; c.work[269 * 480 + 479] = 0x123456;
    CHECK(gc_damage_bits(&c, bits, 30, 17) == 26);
    CHECK(gc_damage_bits(&c, bits, 31, 17) == -1);
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_dirty == 26 && c.last.tiles_emitted == 2 && c.last.tiles_false == 24);
    CHECK(c.ghost[200 * 480 + 100] == 0xABCDEF && c.ghost[269 * 480 + 479] == 0x123456);
    gc_present(&c, NULL, NULL); CHECK(c.last.tiles_dirty == 0);
    gc_free(&c);
}

/* Audit 2026-11-05: four UBSan-confirmed signed overflows on extreme coordinates,
 * plus the line rasterizer running ~4 billion steps for a line across INT range.
 * Run under -fsanitize=undefined -fno-sanitize-recover=all (make ubsan). */
static void test_extreme_coordinates(void) {
    gc_canvas c; CHECK(gc_init(&c, 64, 64) == 0); gc_present(&c, NULL, NULL);
    uint32_t spr[16 * 16]; for (int i = 0; i < 256; ++i) spr[i] = 0xFF00FF00u;
    gc_damage(&c, INT_MAX, 0, 16, 16);                 /* probe 1 */
    gc_rect(&c, INT_MAX, 0, 16, 16, 1);                /* probe 2 */
    gc_blit(&c, INT_MIN, 0, spr, 16, 16, 16);          /* probe 4 */
    gc_damage(&c, INT_MIN, INT_MIN, INT_MAX, INT_MAX); /* ends at -2: outside */
    gc_rect(&c, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 1);
    gc_rect(&c, 0, 0, INT_MAX, INT_MAX, 0);            /* covers the canvas, same colour: clean */
    gc_blit(&c, INT_MAX, INT_MAX, spr, 16, 16, 16);
    gc_blit(&c, 0, 0, spr, INT_MAX, 1, INT_MAX);       /* huge declared size, clipped to 64x1: reads only 64 px */
    gc_present(&c, NULL, NULL);
    CHECK(c.last.tiles_emitted == 4);                  /* only the 64x1 blit row changed (tiles 0..3 of row 0) */
    for (int i = 0; i < 64; ++i) CHECK(c.ghost[i] == 0xFF00FF00u);
    /* probe 3 and friends: lines across the whole int range must finish quickly */
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    gc_line(&c, INT_MIN, 0, INT_MAX, 10, 2);           /* nearly horizontal, crosses canvas around y = 5 */
    gc_line(&c, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 3);/* diagonal through (0,0)..(63,63) region */
    gc_line(&c, INT_MAX, INT_MIN, INT_MIN, INT_MAX, 3);
    gc_line(&c, 0, INT_MIN, 0, INT_MAX, 4);            /* vertical through x = 0 */
    gc_line(&c, INT_MIN, 100, INT_MAX, 100, 5);        /* below the canvas: nothing */
    gc_line(&c, INT_MAX, INT_MAX, INT_MAX, INT_MAX, 5);/* degenerate, outside */
    for (int k = 0; k < 2000; ++k) gc_line(&c, (int)(rnd() % 7 == 0 ? INT_MIN : (int)rnd()), (int)rnd(), (int)rnd(), (int)(rnd() % 5 == 0 ? INT_MAX : (int)rnd()), 6);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    CHECK(ms < 500.0);                                 /* 2006 int-range lines; was unbounded */
    gc_present(&c, NULL, NULL);
    CHECK(c.ghost[0] == 4 || c.ghost[0] == 3 || c.ghost[0] == 6); /* x=0 column was drawn by something */
    CHECK(c.last.tiles_emitted > 0);
    /* a horizontal line that leaves both sides lands on the right row */
    gc_clear(&c, 0); gc_present(&c, NULL, NULL);
    gc_line(&c, -100000, 7, 100000, 7, 9); gc_present(&c, NULL, NULL);
    for (int i = 0; i < 64; ++i) CHECK(c.ghost[7 * 64 + i] == 9);
    CHECK(c.last.tiles_emitted == 4);
    gc_free(&c);
    /* clipped lines are pixel-identical to the unclipped rasterization: compare against a
     * 64-bit brute-force reference Bresenham that plots only inside the canvas */
    gc_canvas big, small; CHECK(gc_init(&big, 300, 300) == 0); CHECK(gc_init(&small, 300, 300) == 0);
    gc_present(&big, NULL, NULL); gc_present(&small, NULL, NULL);
    for (int k = 0; k < 200; ++k) {
        int x0 = (int)(rnd() % 1200) - 450, y0 = (int)(rnd() % 1200) - 450, x1 = (int)(rnd() % 1200) - 450, y1 = (int)(rnd() % 1200) - 450;
        if (k % 4 == 0) { x0 = (int)(rnd() % 600) - 150; y0 = (int)(rnd() % 600) - 150; }   /* some endpoints inside */
        gc_line(&small, x0, y0, x1, y1, 1);           /* bounded path */
        /* reference: unclipped Bresenham, plotting only inside the canvas */
        int64_t dx = llabs((int64_t)x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -llabs((int64_t)y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy, cx = x0, cy = y0;
        for (;;) { if (cx >= 0 && cy >= 0 && cx < 300 && cy < 300) big.work[cy * 300 + cx] = 1; if (cx == x1 && cy == y1) break; int64_t e2 = 2 * err; if (e2 >= dy) { err += dy; cx += sx; } if (e2 <= dx) { err += dx; cy += sy; } }
    }
    int mism = memcmp(big.work, small.work, 300 * 300 * 4) != 0;
    CHECK(mism == 0);
    gc_free(&big); gc_free(&small);
}

int main(void) {
    test_extreme_coordinates();
    test_damage_bits_import();
    test_consumed_hash_does_not_cancel();
    test_first_present_sends_everything();
    test_old_toggle_loses_abc();
    test_aba_is_suppressed();
    test_clear_marks_only_changed_tiles();
    test_bounds();
    test_damage_without_change_is_free();
    test_sink_abort();
    test_fuzz_ghost_tracks_work(480, 270, 300);
    test_fuzz_ghost_tracks_work(1280, 720, 60);
    test_fuzz_ghost_tracks_work(37, 19, 200);       /* odd size, partial tiles both axes */
    test_deterministic_hash();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
