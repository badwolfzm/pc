/* test_superops.c - unit tests for matrixos/superops. Build: make test (runs under ASan+UBSan). */
#include "mx_superops.h"
#include "mx_superops_compat.h"
#include "mx_superops_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(x) do { if (x) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static uint64_t rng_s = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { uint64_t z = (rng_s += 0x9E3779B97F4A7C15ULL); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31); }
static void fill_rand(void *p, size_t n) { uint8_t *b = p; for (size_t i = 0; i < n; ++i) b[i] = (uint8_t)rnd(); }
static float rnd_f32(void) { /* mix of normal magnitudes, including negatives, zero, denormal-free */ uint32_t r = (uint32_t)rnd(); float f = (float)(int32_t)r / 65536.0f; return f; }

/* guarded buffer: canaries before and after, so a kernel overrun is caught even without ASan */
#define GUARD 64
typedef struct { uint8_t *raw; uint8_t *data; size_t n; } gbuf;
static gbuf galloc(size_t n) { gbuf g; g.raw = malloc(n + 2 * GUARD); g.data = g.raw + GUARD; g.n = n; memset(g.raw, 0xC7, GUARD); memset(g.raw + GUARD + n, 0xC7, GUARD); return g; }
static int gcheck(const gbuf *g) { for (size_t i = 0; i < GUARD; ++i) if (g->raw[i] != 0xC7 || g->raw[GUARD + g->n + i] != 0xC7) return 0; return 1; }
static void gfree(gbuf *g) { free(g->raw); }

static const mxs_tier TIERS[] = { MXS_TIER_SCALAR, MXS_TIER_V128, MXS_TIER_V256 };

/* ---------------- vector kernels vs reference, every tier ---------------- */
static void test_vector_equivalence(void) {
    const size_t MAXN = 1037;
    float *a = malloc(MAXN * 4), *b = malloc(MAXN * 4), *d = malloc(MAXN * 4), *r = malloc(MAXN * 4);
    uint8_t *ua = malloc(MAXN), *ub = malloc(MAXN), *ud = malloc(MAXN), *ur = malloc(MAXN);
    for (int t = 0; t < 3; ++t) {
        mxs_tier got = mxs_set_tier(TIERS[t]);
        if (got != TIERS[t]) { printf("  tier %s not available here, skipped\n", mxs_tier_name(TIERS[t])); continue; }
        int bad = 0;
        for (int it = 0; it < 400 && !bad; ++it) {
            size_t n = 1 + (size_t)(rnd() % (it < 100 ? 70 : MAXN));   /* small n first: exercises tails */
            for (size_t i = 0; i < n; ++i) { a[i] = rnd_f32(); b[i] = rnd_f32(); d[i] = rnd_f32(); }
            fill_rand(ua, n); fill_rand(ub, n);
            memcpy(r, d, n * 4); mxs_ref_vadd_f32(r, a, b, n);
            if (mxs_vadd_f32(mxs_span_of(d, n), mxs_cspan_of(a, n), mxs_cspan_of(b, n), n) != MXS_OK || memcmp(d, r, n * 4)) bad |= 1;
            mxs_ref_vmul_f32(r, a, b, n);
            if (mxs_vmul_f32(mxs_span_of(d, n), mxs_cspan_of(a, n), mxs_cspan_of(b, n), n) != MXS_OK || memcmp(d, r, n * 4)) bad |= 2;
            for (size_t i = 0; i < n; ++i) d[i] = rnd_f32();
            memcpy(r, d, n * 4); mxs_ref_vfma_f32(r, a, b, n);
            if (mxs_vfma_f32(mxs_span_of(d, n), mxs_cspan_of(a, n), mxs_cspan_of(b, n), n) != MXS_OK || memcmp(d, r, n * 4)) bad |= 4;
            /* in place: d = d + b */
            memcpy(d, a, n * 4); mxs_ref_vadd_f32(r, a, b, n);
            if (mxs_vadd_f32(mxs_span_of(d, n), mxs_cspan_of(d, n), mxs_cspan_of(b, n), n) != MXS_OK || memcmp(d, r, n * 4)) bad |= 8;
            mxs_ref_vadd_sat_u8(ur, ua, ub, n);
            if (mxs_vadd_sat_u8(mxs_span_of(ud, n), mxs_cspan_of(ua, n), mxs_cspan_of(ub, n), n) != MXS_OK || memcmp(ud, ur, n)) bad |= 16;
            float s1, s2; uint32_t rb1, rb2;
            s1 = mxs_ref_reduce_sum_f32(a, n); if (mxs_reduce_sum_f32(mxs_cspan_of(a, n), n, &s2) != MXS_OK) bad |= 32;
            memcpy(&rb1, &s1, 4); memcpy(&rb2, &s2, 4); if (rb1 != rb2) bad |= 32;
            s1 = mxs_ref_dot_f32(a, b, n); if (mxs_dot_f32(mxs_cspan_of(a, n), mxs_cspan_of(b, n), n, &s2) != MXS_OK) bad |= 64;
            memcpy(&rb1, &s1, 4); memcpy(&rb2, &s2, 4); if (rb1 != rb2) bad |= 64;
            uint8_t mn1, mx1, mn2, mx2; mxs_ref_minmax_u8(ua, n, &mn1, &mx1);
            if (mxs_minmax_u8(mxs_cspan_of(ua, n), n, &mn2, &mx2) != MXS_OK || mn1 != mn2 || mx1 != mx2) bad |= 128;
            uint32_t c2; if (mxs_crc32(mxs_cspan_of(ua, n), n, &c2) != MXS_OK || c2 != mxs_ref_crc32(ua, n)) bad |= 256;
            if (mxs_fill8(mxs_span_of(ud, n), (uint8_t)it, n) != MXS_OK) bad |= 512; else for (size_t i = 0; i < n; ++i) if (ud[i] != (uint8_t)it) { bad |= 512; break; }
            if (mxs_copy(mxs_span_of(ud, n), mxs_cspan_of(ua, n), n) != MXS_OK || memcmp(ud, ua, n)) bad |= 1024;
        }
        printf("  tier %-6s vector kernels == reference: %s (mask 0x%x)\n", mxs_tier_name(got), bad ? "FAIL" : "ok", bad);
        CHECK(bad == 0);
    }
    free(a); free(b); free(d); free(r); free(ua); free(ub); free(ud); free(ur);
}

/* ---------------- pixel kernels vs reference, every tier ---------------- */
static void ref_blit(uint32_t *d, uint32_t dp, int dx, int dy, const uint32_t *s, uint32_t sp, const mxs_rect *sr, mxs_blend m) {
    for (int j = 0; j < sr->h; ++j) for (int i = 0; i < sr->w; ++i) {
        uint32_t *dd = &d[(size_t)(dy + j) * dp + (size_t)(dx + i)];
        *dd = mxs_ref_blend_px(*dd, s[(size_t)(sr->y + j) * sp + (size_t)(sr->x + i)], m);
    }
}
static void test_pixel_equivalence(void) {
    const uint32_t W = 97, H = 41, DP = 103, SP = 99;
    size_t dn = (size_t)(H - 1) * DP + W, sn = (size_t)(H - 1) * SP + W;
    uint32_t *d = malloc(dn * 4), *r = malloc(dn * 4), *s = malloc(sn * 4);
    uint8_t *sh = calloc((dn + 7) / 8, 1), *shr = calloc((dn + 7) / 8, 1);
    for (int t = 0; t < 3; ++t) {
        if (mxs_set_tier(TIERS[t]) != TIERS[t]) continue;
        int bad = 0;
        for (int it = 0; it < 300 && !bad; ++it) {
            fill_rand(d, dn * 4); memcpy(r, d, dn * 4); fill_rand(s, sn * 4);
            /* some fully opaque / fully transparent source alphas, to hit the clamp */
            for (size_t i = 0; i < sn; i += 7) s[i] = (s[i] & 0x00FFFFFFu) | ((i & 1) ? 0xFF000000u : 0u);
            mxs_surface D = { mxs_span_of(d, dn), W, H, DP }; mxs_csurface S = { mxs_cspan_of(s, sn), W, H, SP };
            int w = 1 + (int)(rnd() % W), h = 1 + (int)(rnd() % H);
            mxs_rect sr = { (int)(rnd() % (W - w + 1)), (int)(rnd() % (H - h + 1)), w, h };
            int dx = (int)(rnd() % (W - w + 1)), dy = (int)(rnd() % (H - h + 1));
            mxs_blend m = (mxs_blend)(it % 3);
            ref_blit(r, DP, dx, dy, s, SP, &sr, m);
            if (mxs_blit(&D, dx, dy, &S, &sr, m) != MXS_OK || memcmp(d, r, dn * 4)) bad |= 1;
            /* fill rect */
            uint32_t col = (uint32_t)rnd();
            for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) r[(size_t)(dy + j) * DP + (size_t)(dx + i)] = col;
            mxs_rect fr = { dx, dy, w, h };
            if (mxs_fill_rect(&D, &fr, col) != MXS_OK || memcmp(d, r, dn * 4)) bad |= 2;
            /* dirty blit: shadow bits and count */
            memcpy(r, d, dn * 4); memset(sh, 0, (dn + 7) / 8); memset(shr, 0, (dn + 7) / 8);
            for (size_t i = 0; i < sn; i += 3) s[i] = d[(i < dn) ? i : 0];   /* make some pixels equal */
            uint64_t ch_ref = 0, ch = 0;
            for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) {
                size_t di = (size_t)(dy + j) * DP + (size_t)(dx + i); uint32_t sv = s[(size_t)(sr.y + j) * SP + (size_t)(sr.x + i)];
                if (r[di] != sv) { shr[di >> 3] ^= (uint8_t)(1u << (di & 7)); ch_ref++; } r[di] = sv;
            }
            if (mxs_blit_dirty(&D, dx, dy, &S, &sr, mxs_span_of(sh, (dn + 7) / 8), &ch) != MXS_OK || ch != ch_ref || memcmp(d, r, dn * 4) || memcmp(sh, shr, (dn + 7) / 8)) bad |= 4;
            /* triangle vs reference rows */
            int64_t x[3], y[3];
            for (int k = 0; k < 3; ++k) { x[k] = (int64_t)(rnd() % (W + 40)) - 20; y[k] = (int64_t)(rnd() % (H + 40)) - 20; }
            memcpy(r, d, dn * 4);
            mxs_rect clip = { 0, 0, (int)W, (int)H };
            int64_t area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
            if (area != 0) {
                int64_t minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
                for (int k = 1; k < 3; ++k) { if (x[k] < minx) minx = x[k]; if (x[k] > maxx) maxx = x[k]; if (y[k] < miny) miny = y[k]; if (y[k] > maxy) maxy = y[k]; }
                if (minx < 0) minx = 0;
                if (miny < 0) miny = 0;
                if (maxx > (int64_t)W - 1) maxx = W - 1;
                if (maxy > (int64_t)H - 1) maxy = H - 1;
                for (int64_t py = miny; py <= maxy; ++py) mxs_ref_tri_row(r + (size_t)py * DP, minx, maxx, py, x, y, area, col);
            }
            if (mxs_triangle(&D, &clip, x, y, col) != MXS_OK || memcmp(d, r, dn * 4)) bad |= 8;
        }
        printf("  tier %-6s pixel kernels == reference: %s (mask 0x%x)\n", mxs_tier_name(TIERS[t]), bad ? "FAIL" : "ok", bad);
        CHECK(bad == 0);
    }
    free(d); free(r); free(s); free(sh); free(shr);
}

/* ---------------- refusals: zero, oversize, align, overlap, rect, mode ---------------- */
static int untouched(const uint8_t *p, size_t n, uint8_t v) { for (size_t i = 0; i < n; ++i) if (p[i] != v) return 0; return 1; }
static void test_refusals(void) {
    mxs_set_tier(MXS_TIER_AUTO);
    enum { N = 64 };
    gbuf gd = galloc(N * 4), ga = galloc(N * 4), gb = galloc(N * 4);
    float *d = (float *)gd.data, *a = (float *)ga.data, *b = (float *)gb.data;
    memset(gd.data, 0x11, N * 4); memset(ga.data, 0x22, N * 4); memset(gb.data, 0x33, N * 4);
    mxs_span D = mxs_span_of(d, N); mxs_cspan A = mxs_cspan_of(a, N), B = mxs_cspan_of(b, N);
    /* zero length */
    CHECK(mxs_vadd_f32(D, A, B, 0) == MXS_E_ZERO_LEN);
    CHECK(mxs_fill8(D, 0, 0) == MXS_E_ZERO_LEN);
    CHECK(mxs_copy(D, A, 0) == MXS_E_ZERO_LEN);
    uint32_t c; float f; uint8_t mn, mx;
    CHECK(mxs_crc32(A, 0, &c) == MXS_E_ZERO_LEN);
    CHECK(mxs_reduce_sum_f32(A, 0, &f) == MXS_E_ZERO_LEN);
    CHECK(mxs_dot_f32(A, B, 0, &f) == MXS_E_ZERO_LEN);
    CHECK(mxs_minmax_u8(A, 0, &mn, &mx) == MXS_E_ZERO_LEN);
    /* oversize: one past each span, and absurd */
    CHECK(mxs_vadd_f32(D, A, B, N + 1) == MXS_E_OVERSIZE);
    CHECK(mxs_vadd_f32(D, mxs_cspan_of(a, N - 1), B, N) == MXS_E_OVERSIZE);     /* source shorter than count */
    CHECK(mxs_vadd_f32(mxs_span_of(d, N - 1), A, B, N) == MXS_E_OVERSIZE);     /* destination shorter */
    CHECK(mxs_vfma_f32(D, A, B, UINT64_MAX) == MXS_E_OVERSIZE);
    CHECK(mxs_vadd_sat_u8(mxs_span_of(d, 16), mxs_cspan_of(a, 16), mxs_cspan_of(b, 16), 32) == MXS_E_OVERSIZE);
    CHECK(mxs_fill8(mxs_span_of(d, 16), 0xFF, 32) == MXS_E_OVERSIZE);          /* finding 6: gpu_clear 16-byte buffer, length 32 */
    CHECK(mxs_crc32(mxs_cspan_of(a, 16), 32, &c) == MXS_E_OVERSIZE);          /* finding 6: crc32 16-byte buffer, length 32 */
    CHECK(mxs_copy(D, mxs_cspan_of(a, 10), 11) == MXS_E_OVERSIZE);
    CHECK(mxs_reduce_sum_f32(A, N + 1, &f) == MXS_E_OVERSIZE);
    CHECK(mxs_dot_f32(A, mxs_cspan_of(b, 3), 4, &f) == MXS_E_OVERSIZE);
    CHECK(mxs_minmax_u8(A, (uint64_t)1 << 40, &mn, &mx) == MXS_E_OVERSIZE);
    /* alignment */
    CHECK(mxs_vadd_f32(mxs_span_of((uint8_t *)d + 1, N - 1), A, B, 4) == MXS_E_ALIGN);
    CHECK(mxs_vmul_f32(D, mxs_cspan_of((uint8_t *)a + 2, N - 1), B, 4) == MXS_E_ALIGN);
    /* partial overlap rejected, exact alias allowed */
    CHECK(mxs_vadd_f32(D, mxs_cspan_of(d + 1, N - 1), B, 8) == MXS_E_OVERLAP);
    CHECK(mxs_vadd_f32(mxs_span_of(d + 1, N - 1), mxs_cspan_of(d, N), B, 8) == MXS_E_OVERLAP);
    CHECK(mxs_copy(D, mxs_cspan_of((uint8_t *)d + 8, N * 4 - 8), 16) == MXS_E_OVERLAP);
    CHECK(mxs_copy(D, mxs_cspan_of(d, N * 4), 16) == MXS_E_OVERLAP);           /* copy onto itself is pointless and refused */
    /* null */
    CHECK(mxs_vadd_f32(mxs_span_of(NULL, N), A, B, 4) == MXS_E_ARG);
    CHECK(mxs_crc32(A, 4, NULL) == MXS_E_ARG);
    /* nothing above wrote anything */
    CHECK(untouched(gd.data, N * 4, 0x11) && untouched(ga.data, N * 4, 0x22) && untouched(gb.data, N * 4, 0x33));
    CHECK(gcheck(&gd) && gcheck(&ga) && gcheck(&gb));
    gfree(&gd); gfree(&ga); gfree(&gb);

    /* surfaces */
    enum { W = 32, H = 16, P = 40 };
    gbuf gs = galloc(((H - 1) * P + W) * 4), gt = galloc(((H - 1) * P + W) * 4);
    memset(gs.data, 0x44, gs.n); memset(gt.data, 0x55, gt.n);
    mxs_surface S = { mxs_span_of(gs.data, (H - 1) * P + W), W, H, P };
    mxs_csurface T = { mxs_cspan_of(gt.data, (H - 1) * P + W), W, H, P };
    mxs_rect ok = { 1, 1, 4, 4 }, zero = { 0, 0, 0, 4 }, neg = { -1, 0, 4, 4 }, outside = { 30, 0, 4, 4 }, huge = { 0, 0, INT32_MAX, 1 };
    CHECK(mxs_fill_rect(&S, &zero, 1) == MXS_E_ZERO_LEN);
    CHECK(mxs_fill_rect(&S, &neg, 1) == MXS_E_RECT);
    CHECK(mxs_fill_rect(&S, &outside, 1) == MXS_E_RECT);
    CHECK(mxs_fill_rect(&S, &huge, 1) == MXS_E_RECT);
    CHECK(mxs_fill_rect(&S, NULL, 1) == MXS_E_ARG);
    CHECK(mxs_blit(&S, 29, 0, &T, &ok, MXS_BLEND_COPY) == MXS_E_RECT);      /* dst rect runs off the right edge */
    CHECK(mxs_blit(&S, 0, 0, &T, &outside, MXS_BLEND_COPY) == MXS_E_RECT);  /* src rect outside */
    CHECK(mxs_blit(&S, 0, 0, &T, &ok, (mxs_blend)3) == MXS_E_MODE);
    CHECK(mxs_blit(&S, 0, 0, &T, &zero, MXS_BLEND_ALPHA) == MXS_E_ZERO_LEN);
    mxs_csurface self = { mxs_cspan_of(gs.data, (H - 1) * P + W), W, H, P };
    CHECK(mxs_blit(&S, 0, 0, &self, &ok, MXS_BLEND_COPY) == MXS_E_OVERLAP);
    mxs_surface badpitch = S; badpitch.pitch = W - 1;
    CHECK(mxs_clear(&badpitch, 0) == MXS_E_PITCH);
    mxs_surface toosmall = S; toosmall.px.len -= 1;
    CHECK(mxs_clear(&toosmall, 0) == MXS_E_SURFACE);
    mxs_surface wzero = S; wzero.w = 0;
    CHECK(mxs_clear(&wzero, 0) == MXS_E_ZERO_LEN);
    mxs_surface giant = S; giant.w = giant.pitch = 0xFFFFFFFFu; giant.h = 0xFFFFFFFFu;   /* (h-1)*pitch+w ~ 2^64: must not wrap into "fits" */
    CHECK(mxs_clear(&giant, 0) == MXS_E_OVERSIZE);
    mxs_surface misal = S; misal.px.base = gs.data + 2;
    CHECK(mxs_clear(&misal, 0) == MXS_E_ALIGN);
    uint64_t ch;
    uint8_t shadow[8];
    CHECK(mxs_blit_dirty(&S, 0, 0, &T, &ok, mxs_span_of(shadow, 1), &ch) == MXS_E_OVERSIZE);   /* shadow too small for pixel index (1+3)*40+4 */
    CHECK(mxs_blit_dirty(&S, 0, 0, &T, &ok, mxs_span_of(NULL, 0), &ch) == MXS_E_ARG);
    int64_t tx[3] = { 0, 10, 0 }, ty[3] = { 0, 0, 10 };
    mxs_rect clipout = { 0, 0, W + 1, H };
    CHECK(mxs_triangle(&S, &clipout, tx, ty, 1) == MXS_E_RECT);
    int64_t far[3] = { 1ll << 50, 0, 0 };
    CHECK(mxs_triangle(&S, &ok, far, ty, 1) == MXS_E_OVERSIZE);
    CHECK(untouched(gs.data, gs.n, 0x44) && untouched(gt.data, gt.n, 0x55));
    CHECK(gcheck(&gs) && gcheck(&gt));
    /* line fully outside the surface writes nothing but is accepted (it clips) */
    CHECK(mxs_line(&S, -100, -100, -50, -90, 7) == MXS_OK && untouched(gs.data, gs.n, 0x44));
    gfree(&gs); gfree(&gt);
}

/* ---------------- fuzz: random requests on guarded buffers, every tier ---------------- */
static void test_fuzz(void) {
    enum { CAP = 4096 };
    gbuf g1 = galloc(CAP * 4), g2 = galloc(CAP * 4), g3 = galloc(CAP * 4), gsh = galloc(CAP);
    uint64_t accepted = 0, rejected = 0; int canary_ok = 1;
    for (int t = 0; t < 3; ++t) {
        if (mxs_set_tier(TIERS[t]) != TIERS[t]) continue;
        for (int it = 0; it < 20000; ++it) {
            uint32_t w = (uint32_t)(rnd() % 80), h = (uint32_t)(rnd() % 80), p = (uint32_t)(rnd() % 90);
            uint64_t len1 = rnd() % (CAP + 1), len2 = rnd() % (CAP + 1);
            mxs_surface D = { mxs_span_of(g1.data, len1), w, h, p };
            mxs_csurface S = { mxs_cspan_of(g2.data, len2), (uint32_t)(rnd() % 80), (uint32_t)(rnd() % 80), (uint32_t)(rnd() % 90) };
            mxs_rect r = { (int32_t)(rnd() % 100) - 10, (int32_t)(rnd() % 100) - 10, (int32_t)(rnd() % 90) - 5, (int32_t)(rnd() % 90) - 5 };
            mxs_err e; uint64_t ch; uint32_t c; float f; uint8_t mn, mx;
            switch (rnd() % 9) {
                case 0: e = mxs_fill_rect(&D, &r, (uint32_t)rnd()); break;
                case 1: e = mxs_blit(&D, (int32_t)(rnd() % 100) - 10, (int32_t)(rnd() % 100) - 10, &S, &r, (mxs_blend)(rnd() % 4)); break;
                case 2: e = mxs_blit_dirty(&D, (int32_t)(rnd() % 100) - 10, (int32_t)(rnd() % 100) - 10, &S, &r, mxs_span_of(gsh.data, rnd() % (CAP + 1)), &ch); break;
                case 3: { int64_t x[3], y[3]; for (int k = 0; k < 3; ++k) { x[k] = (int64_t)(rnd() % 200) - 100; y[k] = (int64_t)(rnd() % 200) - 100; } e = mxs_triangle(&D, &r, x, y, 1); break; }
                case 4: e = mxs_line(&D, (int32_t)(rnd() % 400) - 200, (int32_t)(rnd() % 400) - 200, (int32_t)(rnd() % 400) - 200, (int32_t)(rnd() % 400) - 200, 2); break;
                case 5: e = mxs_vadd_f32(mxs_span_of(g1.data + (rnd() % 8), len1), mxs_cspan_of(g2.data, len2), mxs_cspan_of(g3.data, rnd() % (CAP + 1)), rnd() % (CAP + 300)); break;
                case 6: e = mxs_copy(mxs_span_of(g1.data + (rnd() % 64), len1), mxs_cspan_of(g1.data + (rnd() % 64), len2), rnd() % (CAP + 300)); break;
                case 7: e = mxs_crc32(mxs_cspan_of(g2.data, len2), rnd() % (CAP * 4 + 300), &c); break;
                default: e = (rnd() & 1) ? mxs_reduce_sum_f32(mxs_cspan_of(g2.data, len2), rnd() % (CAP + 300), &f) : mxs_minmax_u8(mxs_cspan_of(g2.data, len2), rnd() % (CAP * 4 + 300), &mn, &mx); break;
            }
            if (e == MXS_OK) accepted++; else rejected++;
            if (!(gcheck(&g1) && gcheck(&g2) && gcheck(&g3) && gcheck(&gsh))) { canary_ok = 0; break; }
        }
    }
    printf("  fuzz: %llu accepted, %llu rejected, canaries %s\n", (unsigned long long)accepted, (unsigned long long)rejected, canary_ok ? "intact" : "CORRUPTED");
    CHECK(canary_ok); CHECK(accepted > 1000);   /* the generator must reach the kernels, not only the validator */
    gfree(&g1); gfree(&g2); gfree(&g3); gfree(&gsh);
}

/* ---------------- baseline regressions through the compat shim ---------------- */
static void test_compat(void) {
    mxs_set_tier(MXS_TIER_AUTO);
    const uint32_t ALL = MXC_CAP_SUPEROP | MXC_CAP_GPU;
    float a[8] = {1,2,3,4,5,6,7,8}, b[8] = {10,20,30,40,50,60,70,80}; uint64_t ret = 0;
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_GPU_VADD, a, sizeof a, sizeof a, b, sizeof b, sizeof b, &ret) == 0 && ret == 8 && a[7] == 88.0f);
    float s3[3] = {0};
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_GPU_VADD, a, sizeof a, sizeof a, s3, sizeof s3, sizeof s3, &ret) == -3);           /* source shorter */
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_GPU_VADD, a, sizeof a, 7, b, sizeof b, sizeof b, &ret) == -4);                      /* 7 bytes is not 4-byte elements */
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_GPU_VADD, a, sizeof a, 64, b, sizeof b, 64, &ret) == -5);                           /* beyond capacity */
    float before[8]; memcpy(before, a, sizeof a);
    CHECK(mx_superop_dispatch2(MXC_CAP_SUPEROP, MXC_OP_GPU_VADD, a, sizeof a, sizeof a, b, sizeof b, sizeof b, &ret) == -101 && memcmp(before, a, sizeof a) == 0);  /* no GPU cap: untouched */
    uint8_t buf[16]; memset(buf, 0, sizeof buf);
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_GPU_CLEAR, buf, sizeof buf, sizeof buf, NULL, 0, 0x1FF, &ret) == 0 && ret == 16 && untouched(buf, 16, 0xFF));
    memset(buf, 0, sizeof buf);
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_GPU_CLEAR, buf, 16, 32, NULL, 0, 0xFF, &ret) == -5 && untouched(buf, 16, 0));     /* finding 6 */
    uint8_t payload[32]; memset(payload, 0x5A, sizeof payload);
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_CRC32, payload, 32, 32, NULL, 0, 0, &ret) == 0 && ret == 0x9139d123u);
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_CRC32, payload, 16, 32, NULL, 0, 0, &ret) == -5);                                   /* finding 6 */
    CHECK(mx_superop_dispatch2(MXC_CAP_GPU, MXC_OP_CRC32, payload, 32, 32, NULL, 0, 0, &ret) == -100);
    CHECK(mx_superop_dispatch2(ALL, 7, payload, 32, 32, NULL, 0, 0, &ret) == -1);
    uint8_t dst[8]; memset(dst, 0, 8);
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_MEMCPY, dst, 8, 8, payload, 32, 32, &ret) == 0 && untouched(dst, 8, 0x5A));
    CHECK(mx_superop_dispatch2(ALL, MXC_OP_MEMCPY, dst, 8, 16, payload, 32, 32, &ret) == -5);
}


/* ---------------- blend oracle: the four cases of rcs_test.c test_blend_rules, values taken from rcs.c blend_px ---------------- */
static void test_blend_oracle(void) {
    const uint32_t dst[4] = {0x40102030u, 0xFF808080u, 0x00FFFFFFu, 0x80F0F0F0u};
    const uint32_t src[4] = {0x80FF0000u, 0x00FFFFFFu, 0xFF123456u, 0x7F101010u};
    const uint32_t alpha[4] = {0x40881018u, 0xff808080u, 0x00123456u, 0x80808080u};
    const uint32_t add[4]   = {0xc0ff2030u, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    for (int t = 0; t < 3; ++t) {
        if (mxs_set_tier(TIERS[t]) != TIERS[t]) continue;
        /* run through the kernel path: a 4x1 surface padded to a full vector so the SIMD body, not only the tail, is exercised */
        uint32_t d[16], s[16];
        for (int i = 0; i < 16; ++i) { d[i] = dst[i & 3]; s[i] = src[i & 3]; }
        mxs_surface D = { mxs_span_of(d, 16), 16, 1, 16 }; mxs_csurface S = { mxs_cspan_of(s, 16), 16, 1, 16 }; mxs_rect r = { 0, 0, 16, 1 };
        CHECK(mxs_blit(&D, 0, 0, &S, &r, MXS_BLEND_ALPHA) == MXS_OK);
        for (int i = 0; i < 16; ++i) CHECK(d[i] == alpha[i & 3]);
        for (int i = 0; i < 16; ++i) d[i] = dst[i & 3];
        CHECK(mxs_blit(&D, 0, 0, &S, &r, MXS_BLEND_ADD) == MXS_OK);
        for (int i = 0; i < 16; ++i) CHECK(d[i] == add[i & 3]);
        for (int i = 0; i < 4; ++i) CHECK(mxs_ref_blend_px(dst[i], src[i], MXS_BLEND_ALPHA) == alpha[i] && mxs_ref_blend_px(dst[i], src[i], MXS_BLEND_ADD) == add[i]);
    }
}

/* ---------------- compiler lowering: shapes, legality, and round trip against the C loop ---------------- */
static void test_lowering(void) {
    mxs_set_tier(MXS_TIER_AUTO);
    mxl_loop L; memset(&L, 0, sizeof L);
    L.elem = MXL_F32; L.body = MXL_BODY_ADD; L.dst = 0; L.a = 1; L.b = 2; L.n_known = true; L.n = 100;
    L.len[0] = L.len[1] = L.len[2] = 128;
    mxl_lowered r = mxl_lower(&L);
    CHECK(r.status == MXL_OK && r.kernel == MXL_K_VADD_F32 && !r.in_place && strcmp(r.symbol, "mxs_vadd_f32") == 0);
    L.alias[0][1] = 1; r = mxl_lower(&L); CHECK(r.status == MXL_OK && r.in_place);               /* d = d + b */
    L.alias[0][1] = 2; r = mxl_lower(&L); CHECK(r.status == MXL_SKIP_ALIAS);                     /* may overlap */
    L.alias[0][1] = 0; L.n = 129; r = mxl_lower(&L); CHECK(r.status == MXL_SKIP_LEN);            /* trip count over a known length */
    L.n = 100; L.body = MXL_BODY_MUL_ADD_DST; r = mxl_lower(&L); CHECK(r.status == MXL_OK && r.kernel == MXL_K_VFMA_F32);
    L.body = MXL_BODY_REDUCE_SUM; L.dst = -1; r = mxl_lower(&L); CHECK(r.status == MXL_SKIP_ORDER); /* strict FP: order differs */
    L.fast_math = true; r = mxl_lower(&L); CHECK(r.status == MXL_OK && r.kernel == MXL_K_REDUCE_SUM_F32);
    L.body = MXL_BODY_ADD_SAT; L.dst = 0; r = mxl_lower(&L); CHECK(r.status == MXL_SKIP_TYPE);   /* saturating add is a u8 shape */
    L.elem = MXL_U8; r = mxl_lower(&L); CHECK(r.status == MXL_OK && r.kernel == MXL_K_VADD_SAT_U8 && r.elem_bytes == 1);
    L.body = MXL_BODY_STORE_COPY; L.alias[0][1] = 1; r = mxl_lower(&L); CHECK(r.status == MXL_SKIP_ALIAS); /* copy onto itself */
    L.alias[0][1] = 0; r = mxl_lower(&L); CHECK(r.status == MXL_OK && r.kernel == MXL_K_COPY);
    L.body = MXL_BODY_CRC32; L.dst = -1; r = mxl_lower(&L); CHECK(r.status == MXL_OK && r.kernel == MXL_K_CRC32);
    L.body = (mxl_body)99; r = mxl_lower(&L); CHECK(r.status == MXL_SKIP_ARG);
    CHECK(mxl_lower(NULL).status == MXL_SKIP_ARG);
    /* round trip: the lowered kernel equals the C loop it replaces (two roundings for a*b+d) */
    enum { N = 777 };
    static float a[N], b[N], d1[N], d2[N];
    for (int i = 0; i < N; ++i) { a[i] = rnd_f32(); b[i] = rnd_f32(); d1[i] = d2[i] = rnd_f32(); }
    for (int i = 0; i < N; ++i) d1[i] = a[i] * b[i] + d1[i];          /* what the source loop computes under -ffp-contract=off */
    CHECK(mxs_vfma_f32(mxs_span_of(d2, N), mxs_cspan_of(a, N), mxs_cspan_of(b, N), N) == MXS_OK && memcmp(d1, d2, sizeof d1) == 0);
    static uint8_t u[N], v[N], w1[N], w2[N];
    fill_rand(u, N); fill_rand(v, N);
    for (int i = 0; i < N; ++i) { unsigned s = u[i] + v[i]; w1[i] = (uint8_t)(s > 255 ? 255 : s); }
    CHECK(mxs_vadd_sat_u8(mxs_span_of(w2, N), mxs_cspan_of(u, N), mxs_cspan_of(v, N), N) == MXS_OK && memcmp(w1, w2, N) == 0);
    CHECK(mxs_pixel_ops(MXS_BLEND_COPY, 1280, 720) == 921600ull && mxs_pixel_ops(MXS_BLEND_ALPHA, 1280, 720) == 1843200ull && mxs_pixel_ops(MXS_BLEND_ADD, 3, 2) == 12);
    CHECK(strstr(mxs_tier_cflags(MXS_TIER_V256), "native") == NULL && strstr(mxs_tier_cflags(MXS_TIER_V256), "-mavx2") != NULL);
}

int main(void) {
    printf("mx_superops tests, CPU best tier: %s\n", mxs_tier_name(mxs_detect_tier()));
    printf("[vector equivalence]\n"); test_vector_equivalence();
    printf("[pixel equivalence]\n");  test_pixel_equivalence();
    printf("[refusals]\n");           test_refusals();
    printf("[fuzz]\n");               test_fuzz();
    printf("[compat shim]\n");        test_compat();
    printf("[blend oracle]\n");       test_blend_oracle();
    printf("[lowering]\n");           test_lowering();
    printf("result: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
