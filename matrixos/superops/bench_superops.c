#define _POSIX_C_SOURCE 200809L
/* bench_superops.c - anti-illusion benchmark rows (design doc Phase59 format) for the kernels.
 * Every row: repeated runs with warmup, median and p95, timer floor measured, output consumed
 * into a hash so the compiler cannot drop the work, scalar tier as the baseline. */
#include "mx_superops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y); }
static uint64_t timer_floor(void) { uint64_t best = UINT64_MAX; for (int i = 0; i < 2000; ++i) { uint64_t a = now_ns(), b = now_ns(); if (b >= a && b - a < best) best = b - a; } return best; }
static uint64_t fnv(const void *p, size_t n, uint64_t h) { const uint8_t *b = p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } return h; }

enum { RUNS = 21, WARM = 3 };
static void row(const char *id, const char *metric, mxs_tier tier, uint64_t floor_ns, uint64_t bytes_touched, uint64_t (*fn)(void *), void *ctx) {
    mxs_set_tier(tier);
    uint64_t s[RUNS], consumed = 1469598103934665603ULL;
    for (int i = 0; i < WARM; ++i) consumed ^= fn(ctx);
    for (int i = 0; i < RUNS; ++i) { uint64_t t0 = now_ns(); uint64_t h = fn(ctx); uint64_t t1 = now_ns(); s[i] = t1 - t0; consumed = fnv(&h, 8, consumed); }
    qsort(s, RUNS, sizeof s[0], cmp_u64);
    uint64_t med = s[RUNS / 2], p95 = s[(RUNS * 95) / 100];
    double gbs = med ? (double)bytes_touched / (double)med : 0.0;   /* bytes/ns == GB/s */
    printf("%s,superops_bench,%s,%s,%s,%d,%d,%llu,%llu,%llu,clock_gettime_monotonic,%.2f,%016llx,1,1,%d\n",
           id, metric, mxs_tier_name(tier), bytes_touched ? "runtime-overhead" : "algorithmic", RUNS, WARM,
           (unsigned long long)med, (unsigned long long)p95, (unsigned long long)floor_ns, gbs, (unsigned long long)consumed, floor_ns <= 1000);
}

/* workloads sized to the design doc: one 720p row set / 1M floats */
enum { N = 1 << 20, W = 1280, H = 720 };
typedef struct { float *a, *b, *d; uint8_t *u8a, *u8b, *u8d; uint32_t *fb, *spr; uint8_t *shadow; } ctx_t;

static uint64_t f_vadd(void *p)  { ctx_t *c = p; mxs_vadd_f32(mxs_span_of(c->d, N), mxs_cspan_of(c->a, N), mxs_cspan_of(c->b, N), N); return fnv(c->d, 64, 0) ^ (uint64_t)c->d[N - 1]; }
static uint64_t f_fma(void *p)   { ctx_t *c = p; mxs_vfma_f32(mxs_span_of(c->d, N), mxs_cspan_of(c->a, N), mxs_cspan_of(c->b, N), N); return fnv(c->d + N - 16, 64, 0); }
static uint64_t f_dot(void *p)   { ctx_t *c = p; float r; mxs_dot_f32(mxs_cspan_of(c->a, N), mxs_cspan_of(c->b, N), N, &r); uint32_t b; memcpy(&b, &r, 4); return b; }
static uint64_t f_crc(void *p)   { ctx_t *c = p; uint32_t r; mxs_crc32(mxs_cspan_of(c->u8a, N * 4), N * 4, &r); return r; }
static uint64_t f_sat(void *p)   { ctx_t *c = p; mxs_vadd_sat_u8(mxs_span_of(c->u8d, N * 4), mxs_cspan_of(c->u8a, N * 4), mxs_cspan_of(c->u8b, N * 4), N * 4); return fnv(c->u8d, 64, 0); }
static uint64_t f_clear(void *p) { ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_clear(&fb, 0xFF202020u + (uint32_t)(c->fb[7] & 1)); return c->fb[W * H - 1]; }
static uint64_t f_blit_copy(void *p)  { ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_csurface s = { mxs_cspan_of(c->spr, (size_t)W * H), W, H, W }; mxs_rect r = { 0, 0, W, H }; mxs_blit(&fb, 0, 0, &s, &r, MXS_BLEND_COPY); return c->fb[W * H - 1]; }
static uint64_t f_blit_alpha(void *p) { ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_csurface s = { mxs_cspan_of(c->spr, (size_t)W * H), W, H, W }; mxs_rect r = { 0, 0, W, H }; mxs_blit(&fb, 0, 0, &s, &r, MXS_BLEND_ALPHA); return fnv(c->fb, 256, 0); }
static uint64_t f_blit_add(void *p)   { ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_csurface s = { mxs_cspan_of(c->spr, (size_t)W * H), W, H, W }; mxs_rect r = { 0, 0, W, H }; mxs_blit(&fb, 0, 0, &s, &r, MXS_BLEND_ADD); return fnv(c->fb, 256, 0); }
static uint64_t f_sprites(void *p) {   /* 200 sprites of 64x64, alpha blended: the design doc's budget line */
    ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_csurface s = { mxs_cspan_of(c->spr, (size_t)W * H), W, H, W };
    for (int i = 0; i < 200; ++i) { mxs_rect r = { (i * 37) % (W - 64), (i * 53) % (H - 64), 64, 64 }; mxs_blit(&fb, (i * 61) % (W - 64), (i * 29) % (H - 64), &s, &r, MXS_BLEND_ALPHA); }
    return fnv(c->fb, 256, 0);
}
static uint64_t f_dirty(void *p) { ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_csurface s = { mxs_cspan_of(c->spr, (size_t)W * H), W, H, W }; mxs_rect r = { 0, 0, W, H }; uint64_t ch; mxs_blit_dirty(&fb, 0, 0, &s, &r, mxs_span_of(c->shadow, ((size_t)W * H + 7) / 8), &ch); return ch; }
static uint64_t f_tri(void *p) {       /* 20,000 small triangles, flat: the design doc's 3D character line */
    ctx_t *c = p; mxs_surface fb = { mxs_span_of(c->fb, (size_t)W * H), W, H, W }; mxs_rect clip = { 0, 0, W, H };
    for (int i = 0; i < 20000; ++i) { int64_t x0 = (i * 97) % (W - 24), y0 = (i * 31) % (H - 24); int64_t x[3] = { x0, x0 + 20, x0 + 6 }, y[3] = { y0, y0 + 4, y0 + 20 }; mxs_triangle(&fb, &clip, x, y, 0xFF00FF00u + (uint32_t)i); }
    return fnv(c->fb, 256, 0);
}

int main(void) {
    ctx_t c;
    c.a = malloc(N * 4); c.b = malloc(N * 4); c.d = malloc(N * 4); c.u8a = malloc(N * 4); c.u8b = malloc(N * 4); c.u8d = malloc(N * 4);
    c.fb = malloc((size_t)W * H * 4); c.spr = malloc((size_t)W * H * 4); c.shadow = calloc(((size_t)W * H + 7) / 8, 1);
    uint64_t s = 42;
    for (size_t i = 0; i < N; ++i) { s = s * 6364136223846793005ULL + 1; c.a[i] = (float)(int32_t)(s >> 33) / 1024.0f; c.b[i] = (float)(int32_t)(s >> 20) / 4096.0f; c.d[i] = 0; c.u8a[i*4] = (uint8_t)s; c.u8a[i*4+1] = (uint8_t)(s>>8); c.u8a[i*4+2] = (uint8_t)(s>>16); c.u8a[i*4+3] = (uint8_t)(s>>24); }
    memcpy(c.u8b, c.u8a, N * 4);
    for (size_t i = 0; i < (size_t)W * H; ++i) { s = s * 6364136223846793005ULL + 1; c.fb[i] = (uint32_t)(s >> 32); c.spr[i] = (uint32_t)(s >> 16) | 0x80000000u; }
    uint64_t fl = timer_floor();
    mxs_tier best = mxs_detect_tier();
    printf("evidence_id,harness_id,metric,tier,category,runs,warmup_runs,median_ns,p95_ns,timer_floor_ns,timer_source,gb_per_s,consumed_hash,same_work,output_consumed,timer_floor_checked\n");
    mxs_tier tiers[3] = { MXS_TIER_SCALAR, MXS_TIER_V128, MXS_TIER_V256 };
    for (int t = 0; t < 3; ++t) {
        if (tiers[t] > best) continue;
        row("EV-SOP-VADD-001",  "vadd_f32_1M",            tiers[t], fl, 3ull * N * 4, f_vadd, &c);
        row("EV-SOP-FMA-001",   "vfma_f32_1M",            tiers[t], fl, 4ull * N * 4, f_fma, &c);
        row("EV-SOP-DOT-001",   "dot_f32_1M",             tiers[t], fl, 2ull * N * 4, f_dot, &c);
        row("EV-SOP-SAT-001",   "vadd_sat_u8_4M",         tiers[t], fl, 3ull * N * 4, f_sat, &c);
        row("EV-SOP-CRC-001",   "crc32_4MB",              tiers[t], fl, 1ull * N * 4, f_crc, &c);
        row("EV-SOP-CLEAR-001", "clear_720p",             tiers[t], fl, 1ull * W * H * 4, f_clear, &c);
        row("EV-SOP-BLIT-001",  "blit_copy_720p",         tiers[t], fl, 2ull * W * H * 4, f_blit_copy, &c);
        row("EV-SOP-BLIT-002",  "blit_alpha_720p",        tiers[t], fl, 3ull * W * H * 4, f_blit_alpha, &c);
        row("EV-SOP-BLIT-003",  "blit_add_720p",          tiers[t], fl, 3ull * W * H * 4, f_blit_add, &c);
        row("EV-SOP-SPR-001",   "sprites_200x64x64_alpha", tiers[t], fl, 3ull * 200 * 64 * 64 * 4, f_sprites, &c);
        row("EV-SOP-DIRTY-001", "blit_dirty_720p",        tiers[t], fl, 3ull * W * H * 4, f_dirty, &c);
        row("EV-SOP-TRI-001",   "triangles_20k_flat",     tiers[t], fl, 0, f_tri, &c);
    }
    return 0;
}
