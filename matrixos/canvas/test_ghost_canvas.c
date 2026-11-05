/* Tests for ghost_canvas: correctness of damage tracking and streaming. */
#include "ghost_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
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
