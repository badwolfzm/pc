#include "canvas_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void) {
    /* image blit with sub-rect and geometry checks */
    gc_canvas c; CHECK(gc_init(&c, 100, 60) == 0); gc_present(&c, NULL, NULL);
    uint32_t img[40 * 30]; for (int i = 0; i < 40 * 30; ++i) img[i] = 0xFF000000u | (uint32_t)i;
    CHECK(gc_blit_image(&c, 10, 10, img, 32, 30, 40, 4, 5, 8, 6) == 0);          /* stride 40 > width 32 */
    gc_present(&c, NULL, NULL);
    CHECK(c.ghost[10 * 100 + 10] == (0xFF000000u | (5 * 40 + 4)));
    CHECK(c.ghost[15 * 100 + 17] == (0xFF000000u | (10 * 40 + 11)));
    CHECK(gc_blit_image(&c, 0, 0, img, 32, 30, 40, 30, 0, 8, 6) == GC_ASSET_BAD_GEOMETRY);  /* 30+8 > 32 */
    CHECK(gc_blit_image(&c, 0, 0, img, 32, 30, 40, 0, 28, 8, 6) == GC_ASSET_BAD_GEOMETRY);
    CHECK(gc_blit_image(&c, 0, 0, img, 32, 30, 16, 0, 0, 8, 6) == GC_ASSET_BAD_GEOMETRY);   /* stride < width */
    CHECK(gc_blit_image(&c, 0, 0, NULL, 32, 30, 40, 0, 0, 8, 6) == GC_ASSET_BAD_GEOMETRY);
    gc_present(&c, NULL, NULL); CHECK(c.last.tiles_dirty == 0);                            /* rejected blits draw nothing */
    gc_free(&c);

    /* tiled background restore: build a 480x270 background as tiles (30x17 tiles, edge tiles padded) */
    uint32_t W = 480, H = 270, TW = 30, TH = 17;
    uint32_t *lin = malloc((size_t)W * H * 4), *tiles = calloc((size_t)TW * TH * 256, 4);
    for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) {
        uint32_t v = 0xFF000000u | (y << 9) | x; lin[y * W + x] = v;
        tiles[(((size_t)(y >> 4) * TW + (x >> 4)) * 256) + (y & 15) * 16 + (x & 15)] = v;
    }
    CHECK(gc_init(&c, W, H) == 0);
    gc_blit(&c, 0, 0, lin, (int)W, (int)H, (int)W); gc_present(&c, NULL, NULL);
    gc_rect(&c, 100, 200, 64, 64, 0x123456); gc_present(&c, NULL, NULL);                   /* sprite drawn, crosses the bottom edge tiles */
    CHECK(gc_restore_tiled_bg(&c, tiles, W, H, 100, 200, 64, 64) == 0);
    gc_present(&c, NULL, NULL);
    CHECK(memcmp(c.ghost, lin, (size_t)W * H * 4) == 0);                                  /* background fully restored */
    CHECK(c.last.tiles_emitted == 5 * 5);                                                 /* x tiles 6..10, y tiles 12..16 */
    CHECK(gc_restore_tiled_bg(&c, tiles, W, H, -500, -500, 10, 10) == 0);
    CHECK(gc_restore_tiled_bg(&c, tiles, 100, 100, 0, 0, 10, 10) == GC_ASSET_BAD_GEOMETRY); /* size mismatch */
    gc_present(&c, NULL, NULL); CHECK(c.last.tiles_emitted == 0);
    free(lin); free(tiles); gc_free(&c);

    /* background binding invalidation */
    gc_bg_binding b = { 7, 3, true };
    CHECK(!gc_bg_event_invalidates(&b, 8, 0, 9, false));    /* other asset */
    CHECK(!gc_bg_event_invalidates(&b, 7, 1, 9, false));    /* other mip */
    CHECK(!gc_bg_event_invalidates(&b, 7, 0, 3, false));    /* same generation */
    CHECK(gc_bg_event_invalidates(&b, 7, 0, 4, false) && b.generation == 4);
    CHECK(gc_bg_event_invalidates(&b, 7, 0, 5, true) && !b.bound);
    CHECK(!gc_bg_event_invalidates(&b, 7, 0, 6, false));    /* unbound now */
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
