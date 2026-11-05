#define _POSIX_C_SOURCE 200809L
/* bench_tri_sweep.c - triangle row-span sweep and the bench-backed path check.
 *
 *   bench_tri_sweep          CSV: span_px,tier,scalar_ns_per_row,simd_ns_per_row,auto_ns_per_row,auto_over_scalar
 *   bench_tri_sweep --check  exit 1 if the default path (threshold MXS_TRI_SIMD_MIN_SPAN_DEFAULT) is a wrong choice:
 *       span >= threshold: the chosen SIMD path is slower than forced scalar by more than NOISE (25%);
 *       span <  threshold: the chosen path IS the scalar code, so auto vs scalar is pure machine noise and
 *                          is not compared; instead it fails if forced SIMD beats scalar by more than 2x,
 *                          which would mean the threshold is set too high on this machine.
 *   Medians of RUNS interleaved runs, order rotated each run. Not part of the sanitizer test build.
 */
#include "mx_superops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
enum { W = 2048, H = 1024, RUNS = 11, ROWS_PER_RUN = 200000 };
static const double NOISE = 1.25;
/* right triangles with a span x span bounding box; every row has about `span` pixels.
 * The three paths are timed interleaved, run by run, so machine drift hits all of them alike. */
static void draw(uint32_t *fb, int span, int ntri) {
    mxs_surface s = { mxs_span_of(fb, (size_t)W * H), W, H, W }; mxs_rect clip = { 0, 0, W, H };
    for (int i = 0; i < ntri; ++i) { int64_t x0 = (i * 131) % (W - span - 1), y0 = (i * 71) % (H - span - 1); int64_t x[3] = { x0, x0 + span, x0 }, y[3] = { y0, y0, y0 + span }; mxs_triangle(&s, &clip, x, y, 0xFF00FF00u + (uint32_t)i); }
}
static void run3(uint32_t *fb, int span, double *sc, double *sv, double *au) {
    const uint32_t knobs[3] = { UINT32_MAX, 0, MXS_TRI_SIMD_MIN_SPAN_DEFAULT };
    int ntri = ROWS_PER_RUN / span + 1; double sm[3][RUNS];
    for (int r = 0; r < RUNS; ++r)
        for (int k = 0; k < 3; ++k) {
            int kk = (k + r) % 3;                               /* rotate the order each run */
            mxs_set_tri_simd_min_span(knobs[kk]);
            uint64_t t0 = now_ns(); draw(fb, span, ntri); uint64_t t1 = now_ns();
            sm[kk][r] = (double)(t1 - t0) / ((double)ntri * span);
        }
    for (int k = 0; k < 3; ++k) qsort(sm[k], RUNS, sizeof sm[0][0], cmp);
    *sc = sm[0][RUNS / 2]; *sv = sm[1][RUNS / 2]; *au = sm[2][RUNS / 2];
}
int main(int argc, char **argv) {
    int check = argc > 1 && strcmp(argv[1], "--check") == 0, bad = 0;
    uint32_t *fb = calloc((size_t)W * H, 4);
    const int spans[] = { 8, 16, 20, 32, 48, 64, 96, 128, 256, 512 };
    const mxs_tier tiers[2] = { MXS_TIER_V128, MXS_TIER_V256 }; mxs_tier best = mxs_detect_tier();
    printf("span_px,tier,scalar_ns_per_row,simd_ns_per_row,auto_ns_per_row,auto_over_scalar\n");
    for (int t = 0; t < 2; ++t) {
        if (tiers[t] > best) continue;
        for (size_t i = 0; i < sizeof spans / sizeof spans[0]; ++i) {
            mxs_set_tier(tiers[t]);
            double sc, sv, au; run3(fb, spans[i], &sc, &sv, &au);
            double ratio = au / sc; const char *flag = "";
            if ((uint32_t)spans[i] >= MXS_TRI_SIMD_MIN_SPAN_DEFAULT) { if (ratio > NOISE) { flag = ",SIMD_PATH_SLOWER_THAN_SCALAR"; bad = 1; } }
            else if (sv * 2.0 < sc) { flag = ",SIMD_WOULD_WIN_2X_LOWER_THRESHOLD"; bad = 1; }
            printf("%d,%s,%.1f,%.1f,%.1f,%.2f%s\n", spans[i], mxs_tier_name(tiers[t]), sc, sv, au, ratio, check ? flag : "");
        }
    }
    if (check) { fprintf(stderr, "tri path check: %s (threshold %u px, noise allowance %.0f%%)\n", bad ? "FAIL" : "ok", MXS_TRI_SIMD_MIN_SPAN_DEFAULT, (NOISE - 1) * 100); return bad; }
    return 0;
}
