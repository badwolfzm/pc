/*
 * gc_dirty_oracle.c -- harness-side oracle test for the ghost canvas (matrixos/canvas).
 * Independent of test_ghost_canvas.c: a brute-force before/after diff at tile granularity.
 *
 * Invariants checked over N random frames on several canvas sizes (odd widths, 1x1, 480x270, 1280x720):
 *   1. Before present: every tile whose work pixels differ from ghost pixels is flagged dirty
 *      (a flagged-but-identical tile is allowed: it is a "false" tile the XOR test will drop).
 *   2. After present: ghost == work for every pixel; the sink received exactly the pixels of the
 *      tiles that differed (a forced present, i.e. the first one after init, sends every tile); all dirty bits are zero; tiles_emitted + tiles_false == tiles_dirty.
 *   3. Nothing outside the canvas is written: the canvas buffers are allocated exactly to size, so
 *      run under -fsanitize=address.
 *   4. The same scripted frame sequence on two canvases gives the same ghost hash (determinism).
 *
 * Build: cc -std=c11 -O1 -g -fsanitize=address,undefined -I<canvas dir> gc_dirty_oracle.c <canvas dir>/ghost_canvas.c
 */
#include "ghost_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(name, cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s\n", name); } } while (0)
static uint64_t rng = 0x243F6A8885A308D3ULL;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

typedef struct { uint32_t *got; uint32_t w, h; uint64_t pixels; int bad_rect; } sink_state;
static int sink(void *user, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    sink_state *s = user;
    if (x + w > s->w || y + h > s->h || w == 0 || h == 0) { s->bad_rect++; return 0; }
    for (uint32_t j = 0; j < h; ++j) for (uint32_t i = 0; i < w; ++i) s->got[(y + j) * s->w + x + i] = px[j * stride + i];
    s->pixels += (uint64_t)w * h; return 0;
}

static int tile_dirty(const gc_canvas *c, uint32_t tx, uint32_t ty) {
    uint64_t idx = (uint64_t)ty * c->tw + tx; /* layout per gc_mark_tile: bit = ty*tw + tx */
    return (c->dirty[idx >> 6] >> (idx & 63)) & 1u;
}

static void random_draw(gc_canvas *c, int W, int H) {
    int x = (int)(rnd() % (uint64_t)(W + 40)) - 20, y = (int)(rnd() % (uint64_t)(H + 40)) - 20;
    uint32_t col = (uint32_t)rnd() & 0x00FFFFFFu;
    switch (rnd() % 6) {
    case 0: gc_pset(c, x, y, col); break;
    case 1: gc_rect(c, x, y, (int)(rnd() % 60) - 5, (int)(rnd() % 40) - 5, col); break;
    case 2: gc_line(c, x, y, (int)(rnd() % (uint64_t)(W + 40)) - 20, (int)(rnd() % (uint64_t)(H + 40)) - 20, col); break;
    case 3: { uint32_t src[8 * 8]; for (int i = 0; i < 64; ++i) src[i] = (uint32_t)rnd() & 0x00FFFFFFu; gc_blit(c, x, y, src, 8, 8, 8); break; }
    case 4: if (rnd() % 16 == 0) gc_clear(c, col); else gc_pset(c, x, y, c->work[((uint32_t)(y < 0 ? 0 : y >= H ? H - 1 : y)) * (uint32_t)W + (uint32_t)(x < 0 ? 0 : x >= W ? W - 1 : x)]); break; /* same-colour write: must not emit */
    default: gc_line(c, x, y, x + (int)(rnd() % 5), y, col); break;
    }
}

static void run_size(uint32_t W, uint32_t H, int frames, int draws_per_frame) {
    gc_canvas c; if (gc_init(&c, W, H) != 0) { printf("FAIL gc_init %ux%u\n", W, H); g_fail++; return; }
    uint32_t tw = (W + GC_TILE - 1) / GC_TILE, th = (H + GC_TILE - 1) / GC_TILE;
    sink_state s = { calloc((size_t)W * H, 4), W, H, 0, 0 };
    char nm[128]; int viol_unflagged = 0, viol_ghost = 0, viol_sink = 0, viol_bits = 0, viol_stats = 0, viol_pixels = 0;
    for (int f = 0; f < frames; ++f) {
        for (int d = 0; d < draws_per_frame; ++d) random_draw(&c, (int)W, (int)H);
        /* 1. brute-force diff vs flags, before present */
        uint64_t differing_pixels = 0;
        for (uint32_t ty = 0; ty < th; ++ty) for (uint32_t tx = 0; tx < tw; ++tx) {
            int differs = 0; uint64_t cnt = 0;
            for (uint32_t y = ty * GC_TILE; y < (ty + 1) * GC_TILE && y < H; ++y) for (uint32_t x = tx * GC_TILE; x < (tx + 1) * GC_TILE && x < W; ++x) { cnt++; if (c.work[y * W + x] != c.ghost[y * W + x]) differs = 1; }
            if (differs) { differing_pixels += cnt; if (!tile_dirty(&c, tx, ty)) viol_unflagged++; }
        }
        int forced = c.force; /* a forced present (first frame after init, or after gc_invalidate_all) sends every dirty tile */
        if (forced) differing_pixels = (uint64_t)W * H;
        s.pixels = 0; memcpy(s.got, c.ghost, (size_t)W * H * 4);
        gc_present(&c, sink, &s);
        /* 2. after present */
        if (memcmp(c.ghost, c.work, (size_t)W * H * 4) != 0) viol_ghost++;
        if (memcmp(s.got, c.work, (size_t)W * H * 4) != 0) viol_sink++;
        if (s.pixels != differing_pixels) { viol_pixels++; if (viol_pixels <= 2) printf("   %ux%u frame %d: sink pixels %llu, differing-tile pixels %llu, stats dirty=%u emitted=%u false=%u pixels_emitted=%llu\n", W, H, f, (unsigned long long)s.pixels, (unsigned long long)differing_pixels, c.last.tiles_dirty, c.last.tiles_emitted, c.last.tiles_false, (unsigned long long)c.last.pixels_emitted); }
        for (uint32_t ty = 0; ty < th; ++ty) for (uint32_t tx = 0; tx < tw; ++tx) if (tile_dirty(&c, tx, ty)) viol_bits++;
        if (c.last.tiles_emitted + c.last.tiles_false != c.last.tiles_dirty) viol_stats++;
    }
    snprintf(nm, sizeof nm, "%ux%u: every differing tile was flagged before present (%d misses)", W, H, viol_unflagged); CHECK(nm, viol_unflagged == 0);
    snprintf(nm, sizeof nm, "%ux%u: ghost == work after every present (%d frames off)", W, H, viol_ghost); CHECK(nm, viol_ghost == 0);
    snprintf(nm, sizeof nm, "%ux%u: sink buffer == work after every present (%d frames off)", W, H, viol_sink); CHECK(nm, viol_sink == 0);
    snprintf(nm, sizeof nm, "%ux%u: sink received exactly the pixels of differing tiles (%d frames off)", W, H, viol_pixels); CHECK(nm, viol_pixels == 0);
    snprintf(nm, sizeof nm, "%ux%u: all dirty bits clear after present (%d bits left)", W, H, viol_bits); CHECK(nm, viol_bits == 0);
    snprintf(nm, sizeof nm, "%ux%u: tiles_emitted + tiles_false == tiles_dirty (%d frames off)", W, H, viol_stats); CHECK(nm, viol_stats == 0);
    snprintf(nm, sizeof nm, "%ux%u: sink never got a rect outside the canvas (%d)", W, H, s.bad_rect); CHECK(nm, s.bad_rect == 0);
    free(s.got); gc_free(&c);
}

int main(void) {
    run_size(1, 1, 50, 5); run_size(37, 11, 200, 20); run_size(480, 270, 60, 40); run_size(1280, 720, 10, 60); run_size(17, 1000, 30, 30);
    /* 4. determinism */
    uint64_t h[2]; for (int k = 0; k < 2; ++k) { rng = 12345; gc_canvas c; gc_init(&c, 480, 270); for (int f = 0; f < 30; ++f) { for (int d = 0; d < 40; ++d) random_draw(&c, 480, 270); gc_present(&c, NULL, NULL); } h[k] = gc_ghost_hash(&c); gc_free(&c); }
    CHECK("determinism: same scripted frames, same ghost hash", h[0] == h[1]);
    printf("gc_dirty_oracle: %d passed, %d failed\n", g_pass, g_fail); return g_fail ? 1 : 0;
}
