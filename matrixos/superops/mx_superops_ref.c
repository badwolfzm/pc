/*
 * mx_superops_ref.c - scalar reference implementations for the super-op kernels.
 * Deliberately shares NO code with mx_superops.c / mx_superops_kernels.inc so a
 * test that compares them is comparing two independent implementations. The
 * pixel formulas are copied from contracts/gpu-commands-ref/rcs.c.
 */
#include "mx_superops.h"

static inline float mxs_hsum8(const float l[8]) {
    float a = l[0] + l[1], b = l[2] + l[3], c = l[4] + l[5], d = l[6] + l[7];
    float ab = a + b, cd = c + d;
    return ab + cd;
}

/* Same semantics as rcs.c blend_px (contracts/gpu-commands.md, "Blend arithmetic"). */
uint32_t mxs_ref_blend_px(uint32_t d, uint32_t sp, mxs_blend mode) {
    if (mode == MXS_BLEND_COPY) return sp;
    uint32_t sa = sp >> 24, da = d >> 24;
    uint32_t sr = (sp >> 16) & 0xFF, sg = (sp >> 8) & 0xFF, sb = sp & 0xFF;
    uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    if (mode == MXS_BLEND_ALPHA) {
        /* normative rule, gpu-commands.md section 6: t = s*a + d*(255-a) + 128; out = (t + (t >> 8)) >> 8; dst alpha unchanged */
        uint32_t ia = 255u - sa;
        uint32_t tr = sr * sa + dr * ia + 128u, tg = sg * sa + dg * ia + 128u, tb = sb * sa + db * ia + 128u;
        uint32_t r = (tr + (tr >> 8)) >> 8, g = (tg + (tg >> 8)) >> 8, b = (tb + (tb >> 8)) >> 8;
        return (da << 24) | (r << 16) | (g << 8) | b;
    }
    uint32_t r = sr + dr, g = sg + dg, b = sb + db, a = sa + da;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    if (a > 255u) a = 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}
uint32_t mxs_ref_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
void  mxs_ref_vadd_f32(float *d, const float *a, const float *b, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = a[i] + b[i]; }
void  mxs_ref_vmul_f32(float *d, const float *a, const float *b, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = a[i] * b[i]; }
void  mxs_ref_vfma_f32(float *d, const float *a, const float *b, size_t n) { for (size_t i = 0; i < n; ++i) { float p = a[i] * b[i]; d[i] = p + d[i]; } }
void  mxs_ref_vadd_sat_u8(uint8_t *d, const uint8_t *a, const uint8_t *b, size_t n) { for (size_t i = 0; i < n; ++i) { unsigned s = a[i] + b[i]; d[i] = (uint8_t)(s > 255u ? 255u : s); } }
float mxs_ref_reduce_sum_f32(const float *a, size_t n) { float l[8] = {0}; for (size_t i = 0; i < n; ++i) l[i & 7] += a[i]; return mxs_hsum8(l); }
float mxs_ref_dot_f32(const float *a, const float *b, size_t n) { float l[8] = {0}; for (size_t i = 0; i < n; ++i) { float p = a[i] * b[i]; l[i & 7] += p; } return mxs_hsum8(l); }
void  mxs_ref_minmax_u8(const uint8_t *a, size_t n, uint8_t *mn, uint8_t *mx) { uint8_t lo = 255, hi = 0; for (size_t i = 0; i < n; ++i) { if (a[i] < lo) lo = a[i]; if (a[i] > hi) hi = a[i]; } *mn = lo; *mx = hi; }
/* Verbatim inside test of rcs.c DRAW_TRIANGLES, one row. */
void mxs_ref_tri_row(uint32_t *row, int64_t px0, int64_t px1, int64_t py, const int64_t x[3], const int64_t y[3], int64_t area, uint32_t color) {
    for (int64_t px = px0; px <= px1; ++px) {
        int64_t w0 = (x[1] - px) * (y[2] - py) - (x[2] - px) * (y[1] - py);
        int64_t w1 = (x[2] - px) * (y[0] - py) - (x[0] - px) * (y[2] - py);
        int64_t w2 = area - w0 - w1;
        bool inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
        if (inside) row[px] = color;
    }
}
