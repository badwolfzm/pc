/*
 * gpusim_game.c - drive the simulated GPU with a 720p game-shaped workload and
 * print frame times against the 33.3 ms (30 fps) deadline.
 *
 *   gpusim_game <assetpack-binary> <scratch-dir> [frames] [out.ppm]
 *
 * Builds an asset store on disk (background, sprite atlas, low-poly mesh) with
 * the real packer, grants the assets through the store's jail + SHA-256 path,
 * then runs three scenes on every super-op tier:
 *   2d       background copy + 300 alpha sprites 64x64 + HUD upload/blit + lines + rects
 *   2d+3d    the 2d scene + 5 000 flat triangles from the mesh asset
 *   caps     every per-frame cap at once: ~5.9 M pixel-ops, 20 000 triangles, 2 MiB upload
 * Each frame = client encode + service submit (copy-out, validate, execute) + scanout of dirty tiles.
 * One core, unthrottled: "fps" is how many frames per second the core could produce.
 */
#define _GNU_SOURCE
#include "gpusim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define W 1280
#define H 720
#define ATLAS 512
#define HUD 128
#define MESH_TRIS 20000u

enum { S_BACK = 0, S_BG = 1, S_ATLAS = 2, S_HUD = 3 };
enum { A_BG = 0, A_ATLAS = 1, A_MESH = 2 };

static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int write_file(const char *p, const void *d, size_t n) {
    FILE *f = fopen(p, "wb"); if (!f) return -1;
    size_t w = fwrite(d, 1, n, f); fclose(f); return w == n ? 0 : -1;
}

/* Run the packer; return the manifest hash it prints. */
static int pack(const char *tool, const char *root, const char *args, uint8_t hash[32]) {
    char cmd[8192]; snprintf(cmd, sizeof cmd, "'%s' '%s' %s", tool, root, args);
    FILE *p = popen(cmd, "r"); if (!p) return -1;
    char line[256] = {0}, last[256] = {0};
    while (fgets(line, sizeof line, p)) { memcpy(last, line, sizeof last); }
    if (pclose(p) != 0) return -1;
    last[64] = 0;
    return asset_hash_parse(last, hash);
}

static void make_assets(const char *tool, const char *dir, uint8_t hbg[32], uint8_t hatlas[32], uint8_t hmesh[32]) {
    char root[4096], f[4200];
    snprintf(root, sizeof root, "%s/store", dir);
    /* Background: gradient + grid. */
    uint32_t *bg = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        bg[y * W + x] = 0xFF000000u | ((uint32_t)(x * 255 / W) << 16) | ((uint32_t)(y * 255 / H) << 8) | (((x / 32 + y / 32) & 1) ? 0x60u : 0x20u);
    snprintf(f, sizeof f, "%s/bg.raw", dir); write_file(f, bg, (size_t)W * H * 4); free(bg);
    char args[8192]; snprintf(args, sizeof args, "image '%s' %d %d", f, W, H);
    if (pack(tool, root, args, hbg)) { fprintf(stderr, "pack bg failed\n"); exit(1); }
    /* Atlas: 8x8 soft-edged balls, 64x64 each, with alpha. */
    uint32_t *at = malloc((size_t)ATLAS * ATLAS * 4);
    for (int y = 0; y < ATLAS; ++y) for (int x = 0; x < ATLAS; ++x) {
        int cx = x % 64 - 32, cy = y % 64 - 32, d2 = cx * cx + cy * cy, cell = (y / 64) * 8 + x / 64;
        uint32_t a = d2 >= 900 ? 0u : d2 <= 600 ? 255u : (uint32_t)(255 * (900 - d2) / 300);
        uint32_t col = (uint32_t)(cell * 2654435761u) & 0x00FFFFFFu;
        at[y * ATLAS + x] = (a << 24) | col;
    }
    snprintf(f, sizeof f, "%s/atlas.raw", dir); write_file(f, at, (size_t)ATLAS * ATLAS * 4); free(at);
    snprintf(args, sizeof args, "image '%s' %d %d", f, ATLAS, ATLAS);
    if (pack(tool, root, args, hatlas)) { fprintf(stderr, "pack atlas failed\n"); exit(1); }
    /* Mesh: small triangles, about 20-40 px each, spread over the screen (16.16 fixed). */
    int32_t *m = malloc((size_t)MESH_TRIS * 28);
    for (uint32_t i = 0; i < MESH_TRIS; ++i) {
        int32_t x = (int32_t)(rnd() % (W - 10)), y = (int32_t)(rnd() % (H - 10));
        int32_t *t = m + i * 7;
        t[0] = x << 16; t[1] = y << 16; t[2] = (x + 8) << 16; t[3] = (y + 1) << 16; t[4] = (x + 3) << 16; t[5] = (y + 8) << 16;
        t[6] = (int32_t)(0xFF000000u | (rnd() & 0xFFFFFFu));
    }
    snprintf(f, sizeof f, "%s/mesh.bin", dir); write_file(f, m, (size_t)MESH_TRIS * 28); free(m);
    snprintf(args, sizeof args, "blob '%s'", f);
    if (pack(tool, root, args, hmesh)) { fprintf(stderr, "pack mesh failed\n"); exit(1); }
}

static void must_ok(rcs_result r, const char *what) {
    if (r.status != RCS_OK) { fprintf(stderr, "%s rejected at %u: %s\n", what, r.failing_index, rcs_error_name(r.error)); exit(1); }
}

static void submit_batch(gpusim *g, gpusim_batch *b, const char *what) {
    size_t n = gpusim_batch_end(b);
    if (!n) { fprintf(stderr, "%s: batch overflow\n", what); exit(1); }
    must_ok(gpusim_submit(g, b->buf, n), what);
}

static uint32_t hud_px[HUD * HUD];
static gpusim_batch B;

typedef struct { const char *name; uint32_t sprites, tris, uploads; int lines, rects; } scene;

/* One frame. Returns encode ns via *enc. */
static void frame(gpusim *g, const scene *sc, uint32_t fno, uint64_t *enc_ns) {
    uint64_t e0 = now_ns(), enc = 0;
    /* Frame batch 1: BEGIN_FRAME (so uploads below count against this frame). */
    gpusim_batch_begin(&B, RCS_BATCH_FRAME);
    rcs_begin_frame bf = {fno}; GPUSIM_ADD(&B, RCS_OP_BEGIN_FRAME, bf);
    enc += now_ns() - e0;
    submit_batch(g, &B, "begin");
    /* Resource batches: dynamic HUD/text uploads, 128x128 ARGB = 64 KiB each, 3 per batch. */
    for (uint32_t u = 0; u < sc->uploads; ) {
        e0 = now_ns();
        gpusim_batch_begin(&B, RCS_BATCH_RESOURCE);
        for (int k = 0; k < 3 && u < sc->uploads; ++k, ++u) {
            for (int i = 0; i < HUD * HUD; i += 97) hud_px[i] = 0xC0000000u | (fno * 2654435761u + (uint32_t)i + u);
            rcs_upload up = {S_HUD, {0, 0, HUD, HUD}};
            gpusim_batch_add(&B, RCS_OP_UPLOAD, &up, sizeof up, hud_px, sizeof hud_px);
        }
        enc += now_ns() - e0;
        submit_batch(g, &B, "upload");
    }
    /* Frame batch 2..n: draws, then PRESENT. Split when a batch fills. */
    e0 = now_ns();
    gpusim_batch_begin(&B, RCS_BATCH_FRAME);
    rcs_blit bg = {S_BACK, 0, 0, S_BG, {0, 0, W, H}, RCS_BLEND_COPY, 0}; GPUSIM_ADD(&B, RCS_OP_BLIT, bg);
    if (sc->tris) {
        uint32_t first = (fno * 997u) % (MESH_TRIS - sc->tris + 1);
        rcs_draw_triangles dt = {S_BACK, A_MESH, first, sc->tris, RCS_SLOT_NONE, RCS_SHADE_FLAT}; GPUSIM_ADD(&B, RCS_OP_DRAW_TRIANGLES, dt);
    }
    for (uint32_t i = 0; i < sc->sprites; ++i) {
        int32_t x = (int32_t)((i * 151u + fno * (3u + i % 5u)) % (W + 64)) - 32;
        int32_t y = (int32_t)((i * 89u + fno * (2u + i % 3u)) % (H + 64)) - 32;
        uint32_t cell = i % 64;
        rcs_blit sp = {S_BACK, x, y, S_ATLAS, {(int32_t)(cell % 8) * 64, (int32_t)(cell / 8) * 64, 64, 64}, RCS_BLEND_ALPHA, 0};
        GPUSIM_ADD(&B, RCS_OP_BLIT, sp);
    }
    for (int i = 0; i < sc->rects; ++i) { rcs_fill_rect fr = {S_BACK, {20 + i * 30, 680, 24, 24}, 0xFF00C000u + (uint32_t)i}; GPUSIM_ADD(&B, RCS_OP_FILL_RECT, fr); }
    for (int i = 0; i < sc->lines; ++i) { rcs_line ln = {S_BACK, 0, i * 18, W - 1, (int32_t)((fno + (uint32_t)i * 7) % H), 0xFFFFFFFFu}; GPUSIM_ADD(&B, RCS_OP_LINE, ln); }
    rcs_blit hud = {S_BACK, W - HUD - 16, 16, S_HUD, {0, 0, HUD, HUD}, RCS_BLEND_ALPHA, 0}; GPUSIM_ADD(&B, RCS_OP_BLIT, hud);
    rcs_fence fe = {fno}; GPUSIM_ADD(&B, RCS_OP_FENCE, fe);
    rcs_present pr = {S_BACK, fno}; GPUSIM_ADD(&B, RCS_OP_PRESENT, pr);
    enc += now_ns() - e0;
    submit_batch(g, &B, "draw");
    *enc_ns = enc;
}

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <assetpack> <scratch-dir> [frames] [out.ppm]\n", argv[0]); return 2; }
    const char *tool = argv[1], *dir = argv[2];
    int frames = argc > 3 ? atoi(argv[3]) : 300;
    const char *ppm = argc > 4 ? argv[4] : NULL;
    mkdir(dir, 0755);
    uint8_t hbg[32], hat[32], hme[32];
    uint64_t t0 = now_ns();
    make_assets(tool, dir, hbg, hat, hme);
    double pack_ms = (double)(now_ns() - t0) / 1e6;
    char root[4096]; snprintf(root, sizeof root, "%s/store", dir);

    static const scene scenes[] = {
        {"2d",    300,     0,  1, 40, 20},
        {"2d+3d", 300,  5000,  1, 40, 20},
        {"caps",  590, 20000, 32, 10, 10},
    };
    static const mxs_tier tiers[] = {MXS_TIER_SCALAR, MXS_TIER_V128, MXS_TIER_V256};
    printf("gpusim: 1280x720 ARGB, one core, %d frames per row; deadline 33.33 ms (30 fps)\n", frames);
    printf("assets packed in %.1f ms (bg 3.7 MB, atlas 1 MB, mesh 20000 tris 560 KB)\n", pack_ms);

    for (size_t ti = 0; ti < sizeof tiers / sizeof tiers[0]; ++ti) {
        gpusim_config cfg = { root, ASSET_PLAN_SSD, tiers[ti], RCS_R_ALL };
        gpusim *g;
        if (gpusim_open(&cfg, &g) != ASSET_OK) { fprintf(stderr, "open failed\n"); return 1; }
        mxs_tier got = mxs_get_tier();
        if (got != tiers[ti]) { printf("%-6s (not available on this CPU)\n", mxs_tier_name(tiers[ti])); gpusim_close(g); continue; }
        uint64_t l0 = now_ns();
        if (gpusim_grant(g, A_BG, hbg, RCS_ASSET_IMAGE) || gpusim_grant(g, A_ATLAS, hat, RCS_ASSET_IMAGE) || gpusim_grant(g, A_MESH, hme, RCS_ASSET_MESH)) { fprintf(stderr, "grant failed\n"); return 1; }
        while (gpusim_poll(g) < 3) { if (now_ns() - l0 > 10000000000ull) { fprintf(stderr, "assets never became resident\n"); return 1; } struct timespec ts = {0, 200000}; nanosleep(&ts, NULL); }
        double load_ms = (double)(now_ns() - l0) / 1e6;
        if (ti == 0) { printf("assets resident from disk (SHA-256 verified) in %.1f ms, page cache warm\n", load_ms);
            printf("%-6s %-6s %9s %9s %9s %9s %8s %7s %7s %9s %9s\n", "tier", "scene", "mean_ms", "p50_ms", "p99_ms", "max_ms", "fps", "miss", "enc_ms", "scan_ms", "Mpxops");
        }

        gpusim_batch_begin(&B, RCS_BATCH_RESOURCE);
        rcs_create_surface c0 = {S_BACK, W, H, RCS_FMT_ARGB8888}, c1 = {S_BG, W, H, RCS_FMT_ARGB8888}, c2 = {S_ATLAS, ATLAS, ATLAS, RCS_FMT_ARGB8888}, c3 = {S_HUD, HUD, HUD, RCS_FMT_ARGB8888};
        GPUSIM_ADD(&B, RCS_OP_CREATE_SURFACE, c0); GPUSIM_ADD(&B, RCS_OP_CREATE_SURFACE, c1); GPUSIM_ADD(&B, RCS_OP_CREATE_SURFACE, c2); GPUSIM_ADD(&B, RCS_OP_CREATE_SURFACE, c3);
        rcs_bind_asset b0 = {A_BG, S_BG}, b1 = {A_ATLAS, S_ATLAS};
        GPUSIM_ADD(&B, RCS_OP_BIND_ASSET, b0); GPUSIM_ADD(&B, RCS_OP_BIND_ASSET, b1);
        submit_batch(g, &B, "setup");

        uint32_t fno = 1;
        uint64_t *ft = malloc(sizeof(uint64_t) * (size_t)frames);
        for (size_t si = 0; si < sizeof scenes / sizeof scenes[0]; ++si) {
            const scene *sc = &scenes[si];
            for (int w = 0; w < 10; ++w) { uint64_t e; frame(g, sc, fno++, &e); }   /* warm-up */
            gpusim_stats s0, s1; gpusim_stats_get(g, &s0);
            uint64_t enc_tot = 0, px = 0, miss = 0;
            for (int f = 0; f < frames; ++f) {
                uint64_t a = now_ns(), e;
                frame(g, sc, fno++, &e);
                ft[f] = now_ns() - a; enc_tot += e;
                px += gpusim_client(g)->frame_pixel_ops;
                if (ft[f] > 33333333ull) miss++;
            }
            gpusim_stats_get(g, &s1);
            uint64_t sum = 0; for (int f = 0; f < frames; ++f) sum += ft[f];
            qsort(ft, (size_t)frames, sizeof ft[0], cmp_u64);
            double mean = (double)sum / frames / 1e6;
            printf("%-6s %-6s %9.2f %9.2f %9.2f %9.2f %8.1f %7lu %7.2f %9.3f %9.2f\n", mxs_tier_name(got), sc->name, mean,
                   (double)ft[frames / 2] / 1e6, (double)ft[(frames * 99) / 100] / 1e6, (double)ft[frames - 1] / 1e6, 1000.0 / mean,
                   (unsigned long)miss, (double)enc_tot / frames / 1e6, (double)(s1.scanout_ns - s0.scanout_ns) / frames / 1e6, (double)px / frames / 1e6);
        }
        free(ft);
        if (ppm && ti + 1 == sizeof tiers / sizeof tiers[0]) {
            uint32_t fw, fh; const uint32_t *fb = gpusim_front(g, &fw, &fh);
            FILE *o = fopen(ppm, "wb");
            if (o && fb) { fprintf(o, "P6\n%u %u\n255\n", fw, fh); for (size_t i = 0; i < (size_t)fw * fh; ++i) { uint8_t rgb[3] = {(uint8_t)(fb[i] >> 16), (uint8_t)(fb[i] >> 8), (uint8_t)fb[i]}; fwrite(rgb, 1, 3, o); } }
            if (o) fclose(o);
        }
        gpusim_close(g);
    }
    return 0;
}
