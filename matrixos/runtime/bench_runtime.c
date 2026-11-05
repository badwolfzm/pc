/* bench_runtime.c - service cost per 720p frame through the whole path:
 * ring copy-out -> rcs validate+execute -> tile copy + ghost present -> sink.
 * usage: bench_runtime <out.csv> [frames]
 * Evidence rules (matrixos/tests): output consumed (sink hashes every tile and the final
 * ghost hash is printed), >=31 timed frames after >=5 warmups, median and p95, timer floor. */
#define _GNU_SOURCE
#include "mx_render.h"
#include "mx_rcs_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define W 1280u
#define H 720u
#define NSPR 16

typedef struct { uint64_t hash, tiles, bytes, ns; } hsink;
static int hs_tile(mx_render_sink *k, uint32_t ci, uint32_t f, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *px, uint32_t stride) {
    (void)ci; (void)f; (void)x; (void)y; hsink *s = k->user; uint64_t hh = s->hash, t0 = mx_now_ns();
    for (uint32_t j = 0; j < h; ++j) for (uint32_t i = 0; i < w; ++i) hh = (hh ^ px[j * stride + i]) * 1099511628211ull;   /* the encoder's input, consumed */
    s->hash = hh; s->tiles++; s->bytes += (uint64_t)w * h * 4; s->ns += mx_now_ns() - t0; return 0;
}

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static uint64_t pct(uint64_t *v, int n, double p) { qsort(v, (size_t)n, sizeof *v, cmp_u64); int i = (int)(p * (n - 1) + 0.5); return v[i]; }

typedef struct { int x, y, vx, vy; } spr;
typedef struct { const char *name; int mode; } scene;   /* 0 static, 1 sprites, 2 scroll (full redraw), 3 sprites+200 lines */

static uint32_t build_frame(uint8_t *buf, uint32_t cap, uint32_t f, int mode, spr *s, spr *prev) {
    mxb b; mxb_begin(&b, buf, cap, RCS_BATCH_FRAME);
    mxb_begin_frame(&b, f);
    if (f == 1 || mode == 2) {   /* key frame / scroll: whole background, offset by frame in scroll mode */
        int off = mode == 2 ? (int)(f % 64) : 0;
        mxb_blit(&b, 0, -off, 0, 1, (rcs_rect){ 0, 0, (int32_t)W, (int32_t)H }, RCS_BLEND_COPY);
        if (off) mxb_blit(&b, 0, (int32_t)W - off, 0, 1, (rcs_rect){ 0, 0, off, (int32_t)H }, RCS_BLEND_COPY);
    }
    if (mode == 1 || mode == 3) {
        for (int i = 0; i < NSPR; ++i) {   /* restore the background under the old sprite, from the bg surface */
            mxb_blit(&b, 0, prev[i].x, prev[i].y, 1, (rcs_rect){ prev[i].x, prev[i].y, 64, 64 }, RCS_BLEND_COPY);
        }
        for (int i = 0; i < NSPR; ++i) mxb_blit(&b, 0, s[i].x, s[i].y, 2, (rcs_rect){ 0, 0, 64, 64 }, RCS_BLEND_ALPHA);
        mxb_fill_rect(&b, 0, (rcs_rect){ 8, 8, 200, 24 }, 0xFF000000u | (f * 2654435761u >> 8));   /* HUD changes every frame */
        if (mode == 3) for (int i = 0; i < 200; ++i) mxb_line(&b, 0, (int32_t)((f * 7 + i * 13) % W), 100, (int32_t)((f * 11 + i * 29) % W), 600, 0xFFFFFF00u ^ (uint32_t)i);
    }
    mxb_present(&b, 0, f);
    return mxb_end(&b);
}

int main(int argc, char **argv) {
    const char *csv = argc > 1 ? argv[1] : "bench.csv";
    int frames = argc > 2 ? atoi(argv[2]) : 101;
    if (frames < 31) frames = 31;
    const int warm = 5;
    FILE *out = fopen(csv, "w");
    if (!out) { perror(csv); return 1; }
    fprintf(out, "scene,w,h,frames,build_us_med,copy_us_med,submit_us_med,composite_us_med,sink_us_med,service_us_med,service_us_p95,tiles_med,bytes_med,pixel_ops_med,consumed_hash,timer_floor_ns\n");

    /* timer floor */
    uint64_t fl[1001]; for (int i = 0; i <= 1000; ++i) fl[i] = mx_now_ns(); for (int i = 0; i < 1000; ++i) fl[i] = fl[i + 1] - fl[i]; uint64_t floor_ns = pct(fl, 1000, 0.5);

    scene scenes[] = { { "static", 0 }, { "sprites16", 1 }, { "sprites16_lines200", 3 }, { "scroll_full", 2 } };
    for (unsigned sc = 0; sc < sizeof scenes / sizeof scenes[0]; ++sc) {
        hsink hs = { 1469598103934665603ull, 0, 0, 0 }; mx_render_sink sink = { hs_tile, NULL, &hs };
        mx_render_service s; mx_render_init(&s, NULL, &sink);
        mx_ring *r = mmap(NULL, sizeof *r, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); mx_ring_init(r, sizeof *r);
        int ci = mx_render_attach(&s, r, 1, RCS_R_ALL);
        /* resources: 0 display, 1 background (uploaded procedurally in 64 KiB pieces), 2 sprite */
        uint8_t *p = mx_ring_reserve(r, RCS_MAX_BATCH_BYTES); mxb b; mxb_begin(&b, p, RCS_MAX_BATCH_BYTES, RCS_BATCH_RESOURCE);
        mxb_create_surface(&b, 0, W, H, RCS_FMT_ARGB8888); mxb_create_surface(&b, 1, W, H, RCS_FMT_ARGB8888); mxb_create_surface(&b, 2, 64, 64, RCS_FMT_ARGB8888);
        uint32_t spr_px[64 * 64]; for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { int dx = x - 32, dy = y - 32; uint32_t a = dx * dx + dy * dy < 900 ? 255u : (dx * dx + dy * dy < 1024 ? 128u : 0u); spr_px[y * 64 + x] = (a << 24) | ((uint32_t)(x * 4) << 16) | ((uint32_t)(y * 4) << 8) | 0x80u; }
        mxb_upload(&b, 2, (rcs_rect){ 0, 0, 64, 64 }, spr_px, sizeof spr_px);
        mx_ring_publish(r, p, mxb_end(&b)); mx_render_pump(&s); rcs_result res; mx_ring_get_result(r, &res);
        if (res.status != RCS_OK) { printf("setup rejected: %s\n", rcs_error_name(res.error)); return 1; }
        static uint32_t row[W * 12];   /* 12 rows = 61440 bytes per UPLOAD */
        for (uint32_t y = 0; y < H; y += 12) {
            uint32_t hh = y + 12 <= H ? 12 : H - y;
            for (uint32_t j = 0; j < hh; ++j) for (uint32_t x = 0; x < W; ++x) row[j * W + x] = 0xFF000000u | ((x * 255 / W) << 16) | (((y + j) * 255 / H) << 8) | (((x / 40 + (y + j) / 40) & 1) ? 0x60u : 0x20u);
            p = mx_ring_reserve(r, RCS_MAX_BATCH_BYTES); mxb_begin(&b, p, RCS_MAX_BATCH_BYTES, RCS_BATCH_RESOURCE);
            mxb_upload(&b, 1, (rcs_rect){ 0, (int32_t)y, (int32_t)W, (int32_t)hh }, row, hh * W * 4); mx_ring_publish(r, p, mxb_end(&b));
            mx_render_pump(&s); mx_ring_get_result(r, &res); if (res.status != RCS_OK) { printf("upload rejected: %s\n", rcs_error_name(res.error)); return 1; }
            s.clients[ci].rcs.frame_upload = 0;   /* bench setup: the 2 MiB per-frame upload budget is for frames, not for loading */
        }
        spr sp[NSPR], prev[NSPR];
        for (int i = 0; i < NSPR; ++i) { sp[i] = (spr){ (i * 173) % (int)(W - 64), (i * 97) % (int)(H - 64), 3 + i % 5, 2 + i % 3 }; prev[i] = sp[i]; }
        int n = frames; uint64_t *tb = calloc((size_t)n, 8), *tc = calloc((size_t)n, 8), *ts = calloc((size_t)n, 8), *tk = calloc((size_t)n, 8), *tt = calloc((size_t)n, 8), *tiles = calloc((size_t)n, 8), *bytes = calloc((size_t)n, 8), *pops = calloc((size_t)n, 8), *tsk = calloc((size_t)n, 8);
        for (int f = 1; f <= warm + n; ++f) {
            uint64_t t0 = mx_now_ns();
            p = mx_ring_reserve(r, RCS_MAX_BATCH_BYTES);
            uint32_t len = build_frame(p, RCS_MAX_BATCH_BYTES, (uint32_t)f, scenes[sc].mode, sp, prev);
            if (!len) { printf("batch overflow\n"); return 1; }
            mx_ring_publish(r, p, len);
            uint64_t t1 = mx_now_ns();
            mx_render_client_stats before = s.clients[ci].stats; uint64_t tiles0 = hs.tiles, bytes0 = hs.bytes, sink0 = hs.ns;
            mx_render_pump(&s);
            uint64_t t2 = mx_now_ns();
            mx_ring_get_result(r, &res);
            if (res.status != RCS_OK) { printf("%s frame %d rejected: %s at %u\n", scenes[sc].name, f, rcs_error_name(res.error), res.failing_index); return 1; }
            if (f > warm) { int k = f - warm - 1; mx_render_client_stats a = s.clients[ci].stats;
                tb[k] = t1 - t0; tc[k] = a.ns_copy - before.ns_copy; ts[k] = a.ns_submit - before.ns_submit; tk[k] = a.ns_composite - before.ns_composite; tt[k] = t2 - t1;
                tiles[k] = hs.tiles - tiles0; bytes[k] = hs.bytes - bytes0; pops[k] = res.pixel_ops; tsk[k] = hs.ns - sink0; }
            for (int i = 0; i < NSPR; ++i) { prev[i] = sp[i]; sp[i].x += sp[i].vx; sp[i].y += sp[i].vy; if (sp[i].x < 0 || sp[i].x > (int)W - 64) { sp[i].vx = -sp[i].vx; sp[i].x += 2 * sp[i].vx; } if (sp[i].y < 0 || sp[i].y > (int)H - 64) { sp[i].vy = -sp[i].vy; sp[i].y += 2 * sp[i].vy; } }
        }
        uint64_t hash = hs.hash ^ mx_render_display_hash(&s, ci);
        uint64_t med_t = pct(tt, n, 0.5), p95_t = pct(tt, n, 0.95);
        fprintf(out, "%s,%u,%u,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%llu,%llu,%llu,%016llx,%llu\n", scenes[sc].name, W, H, n,
                pct(tb, n, 0.5) / 1e3, pct(tc, n, 0.5) / 1e3, pct(ts, n, 0.5) / 1e3, pct(tk, n, 0.5) / 1e3, pct(tsk, n, 0.5) / 1e3, med_t / 1e3, p95_t / 1e3,
                (unsigned long long)pct(tiles, n, 0.5), (unsigned long long)pct(bytes, n, 0.5), (unsigned long long)pct(pops, n, 0.5), (unsigned long long)hash, (unsigned long long)floor_ns);
        printf("%-20s service median %7.1f us  p95 %7.1f us  (copy %.1f, submit %.1f, composite %.1f of which sink %.1f)  tiles %llu  bytes %llu  budget 33333 us -> %.1f%%\n",
               scenes[sc].name, med_t / 1e3, p95_t / 1e3, pct(tc, n, 0.5) / 1e3, pct(ts, n, 0.5) / 1e3, pct(tk, n, 0.5) / 1e3, pct(tsk, n, 0.5) / 1e3,
               (unsigned long long)pct(tiles, n, 0.5), (unsigned long long)pct(bytes, n, 0.5), 100.0 * (double)p95_t / 33333333.0);
        free(tb); free(tc); free(ts); free(tk); free(tt); free(tiles); free(bytes); free(pops); free(tsk);
        mx_render_free(&s); munmap(r, sizeof *r);
    }
    fclose(out);
    printf("timer floor %llu ns; wrote %s\n", (unsigned long long)floor_ns, csv);
    return 0;
}
