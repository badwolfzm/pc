#define _POSIX_C_SOURCE 199309L
/* Per-frame cost of the compositor at 480x270 and 1280x720 under four
 * workloads.  Sink is a memcpy into a client buffer (what an encoder's
 * input copy costs); encoding itself is not modelled. */
#include "ghost_canvas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec; }
typedef struct { uint32_t *fb; uint32_t w; } client;
static int sink(void *u, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    client *cl = (client *)u;
    for (uint32_t j = 0; j < h; ++j) memcpy(cl->fb + (size_t)(y + j) * cl->w + x, px + (size_t)j * stride, (size_t)w * 4);
    return 0;
}
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

typedef void (*scene_fn)(gc_canvas *c, int f, uint32_t *spr);
static void scene_static(gc_canvas *c, int f, uint32_t *s) { (void)c;(void)f;(void)s; }
static void scene_sprite(gc_canvas *c, int f, uint32_t *s) {         /* one 64x64 sprite moving over a static background */
    int x = (f * 5) % (int)(c->w - 64), y = (f * 3) % (int)(c->h - 64);
    int px = ((f - 1) * 5) % (int)(c->w - 64), py = ((f - 1) * 3) % (int)(c->h - 64);
    gc_rect(c, px, py, 64, 64, 0x202020);          /* restore background */
    gc_blit(c, x, y, s, 64, 64, 64);
}
static void scene_hud(gc_canvas *c, int f, uint32_t *s) {            /* ~10% of the screen: 20 sprites + a HUD bar */
    for (int i = 0; i < 20; ++i) {
        int x = (i * 97 + f * 4) % (int)(c->w - 48), y = (i * 53 + f * 2) % (int)(c->h - 48);
        gc_blit(c, x, y, s + (i & 3), 48, 48, 64);
    }
    gc_rect(c, 0, 0, (int)c->w, 24, 0x303030u + (uint32_t)(f & 7));
}
static void scene_full(gc_canvas *c, int f, uint32_t *s) {           /* every pixel changes every frame */
    (void)s; gc_clear(c, 0x101010u + (uint32_t)(f & 0x1F));
}

static void run(const char *name, uint32_t w, uint32_t h, scene_fn scene, FILE *csv) {
    enum { FRAMES = 60, RUNS = 9 };
    uint32_t spr[64 * 64 + 4]; for (int i = 0; i < 64 * 64 + 4; ++i) spr[i] = 0xFF000000u | (uint32_t)(i * 2654435761u);
    uint64_t draw_ns[RUNS], present_ns[RUNS], bytes = 0, tiles = 0;
    client cl = { calloc((size_t)w * h, 4), w };
    for (int r = 0; r < RUNS; ++r) {
        gc_canvas c; gc_init(&c, w, h); gc_clear(&c, 0x202020); gc_present(&c, sink, &cl);
        uint64_t d = 0, p = 0;
        for (int f = 1; f <= FRAMES; ++f) {
            uint64_t t0 = now_ns(); scene(&c, f, spr);
            uint64_t t1 = now_ns(); gc_present(&c, sink, &cl);
            uint64_t t2 = now_ns();
            d += t1 - t0; p += t2 - t1;
            if (r == RUNS - 1) { bytes += c.last.bytes_emitted; tiles += c.last.tiles_emitted; }
        }
        draw_ns[r] = d / FRAMES; present_ns[r] = p / FRAMES;
        gc_free(&c);
    }
    /* consume the output: hash the whole client buffer so nothing is dead code */
    uint64_t hsh = 1469598103934665603ULL; const uint8_t *hp = (const uint8_t *)cl.fb;
    for (size_t i = 0; i < (size_t)w * h * 4; ++i) { hsh ^= hp[i]; hsh *= 1099511628211ULL; }
    free(cl.fb);
    qsort(draw_ns, RUNS, 8, cmp_u64); qsort(present_ns, RUNS, 8, cmp_u64);
    double frame_bytes = (double)w * h * 4;
    printf("%-9s %4ux%-4u draw %7.3f ms  present %7.3f ms  tiles/frame %5.0f  bytes/frame %9.0f (%5.1f%% of %.1f MB) client-hash %016llx\n",
           name, w, h, draw_ns[RUNS / 2] / 1e6, present_ns[RUNS / 2] / 1e6, (double)tiles / FRAMES, (double)bytes / FRAMES,
           100.0 * bytes / FRAMES / frame_bytes, frame_bytes / 1048576.0, (unsigned long long)hsh);
    if (csv) fprintf(csv, "%s,%u,%u,%.1f,%.1f,%.0f,%.0f\n", name, w, h, draw_ns[RUNS / 2] / 1e3, present_ns[RUNS / 2] / 1e3, (double)tiles / FRAMES, (double)bytes / FRAMES);
}

int main(int argc, char **argv) {
    FILE *csv = argc > 1 ? fopen(argv[1], "w") : NULL;
    if (csv) fprintf(csv, "scene,w,h,draw_us,present_us,tiles_per_frame,bytes_per_frame\n");
    printf("GC_TILE=%d  bitmap bytes: 480x270=%zu  1280x720=%zu\n", GC_TILE, gc_bitmap_bytes(480, 270), gc_bitmap_bytes(1280, 720));
    uint32_t sizes[2][2] = {{480, 270}, {1280, 720}};
    for (int s = 0; s < 2; ++s) {
        run("static", sizes[s][0], sizes[s][1], scene_static, csv);
        run("sprite", sizes[s][0], sizes[s][1], scene_sprite, csv);
        run("hud10pct", sizes[s][0], sizes[s][1], scene_hud, csv);
        run("full", sizes[s][0], sizes[s][1], scene_full, csv);
    }
    if (csv) fclose(csv);
    return 0;
}
