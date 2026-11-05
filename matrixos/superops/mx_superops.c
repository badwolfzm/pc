/* mx_superops.c - bounded SIMD super-op kernels. See mx_superops.h. */
#include "mx_superops.h"
#include <string.h>


/* ------------------------------------------------------------------ */
/* scalar tier (self-contained; the test references live in mx_superops_ref.c) */
/* ------------------------------------------------------------------ */
static inline float mxs_hsum8(const float l[8]) {
    float a = l[0] + l[1], b = l[2] + l[3], c = l[4] + l[5], d = l[6] + l[7];
    float ab = a + b, cd = c + d;
    return ab + cd;
}
static inline uint32_t blend_alpha_px(uint32_t d, uint32_t sp) {
    uint32_t sa = sp >> 24, ia = 255u - sa;
    uint32_t tb = (sp & 0xFFu) * sa + (d & 0xFFu) * ia + 128u;
    uint32_t tg = ((sp >> 8) & 0xFFu) * sa + ((d >> 8) & 0xFFu) * ia + 128u;
    uint32_t tr = ((sp >> 16) & 0xFFu) * sa + ((d >> 16) & 0xFFu) * ia + 128u;
    return (d & 0xFF000000u) | (((tr + (tr >> 8)) >> 8) << 16) | (((tg + (tg >> 8)) >> 8) << 8) | ((tb + (tb >> 8)) >> 8);
}
static void s_fill32(uint32_t *d, uint32_t c, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = c; }
static void s_fill8(uint8_t *d, uint8_t c, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = c; }
static void s_copy(uint8_t *d, const uint8_t *s, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = s[i]; }
static void s_blend_alpha(uint32_t *d, const uint32_t *s, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = blend_alpha_px(d[i], s[i]); }
static void s_add_sat_bytes(uint8_t *d, const uint8_t *a, const uint8_t *b, size_t n) { for (size_t i = 0; i < n; ++i) { unsigned s = a[i] + b[i]; d[i] = (uint8_t)(s > 255u ? 255u : s); } }
static void s_vadd_f32(float *d, const float *a, const float *b, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = a[i] + b[i]; }
static void s_vmul_f32(float *d, const float *a, const float *b, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = a[i] * b[i]; }
static void s_vfma_f32(float *d, const float *a, const float *b, size_t n) { for (size_t i = 0; i < n; ++i) { float p = a[i] * b[i]; d[i] = p + d[i]; } }
static float s_reduce_sum_f32(const float *a, size_t n) { float l[8] = {0}; for (size_t i = 0; i < n; ++i) l[i & 7] += a[i]; return mxs_hsum8(l); }
static float s_dot_f32(const float *a, const float *b, size_t n) { float l[8] = {0}; for (size_t i = 0; i < n; ++i) { float p = a[i] * b[i]; l[i & 7] += p; } return mxs_hsum8(l); }
static void s_minmax_u8(const uint8_t *a, size_t n, uint8_t *mn, uint8_t *mx) { uint8_t lo = 255, hi = 0; for (size_t i = 0; i < n; ++i) { if (a[i] < lo) lo = a[i]; if (a[i] > hi) hi = a[i]; } *mn = lo; *mx = hi; }
static uint64_t s_dirty_row(uint32_t *d, const uint32_t *s, size_t w, uint8_t *sh, uint64_t bit0) {
    uint64_t ch = 0;
    for (size_t i = 0; i < w; ++i) { if (d[i] != s[i]) { uint64_t bit = bit0 + i; sh[bit >> 3] ^= (uint8_t)(1u << (bit & 7)); ch++; } d[i] = s[i]; }
    return ch;
}
static void s_tri_row(uint32_t *row, int64_t px0, int64_t px1, int64_t py, const int64_t x[3], const int64_t y[3], int64_t area, uint32_t color) {
    int64_t w0 = (x[1] - px0) * (y[2] - py) - (x[2] - px0) * (y[1] - py);
    int64_t w1 = (x[2] - px0) * (y[0] - py) - (x[0] - px0) * (y[2] - py);
    int64_t dw0 = y[1] - y[2], dw1 = y[2] - y[0];
    for (int64_t px = px0; px <= px1; ++px, w0 += dw0, w1 += dw1) {
        int64_t w2 = area - w0 - w1;
        bool in = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
        if (in) row[px] = color;
    }
}
static uint32_t s_crc32(const uint8_t *p, size_t n);  /* slicing-by-4, below */

/* CRC32 slicing-by-8 (same polynomial as the reference). */
static uint32_t crc_tab[8][256]; static int crc_tab_ready;
static void crc_tab_init(void) {
    if (crc_tab_ready) return;
    for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); crc_tab[0][i] = c; }
    for (int k = 1; k < 8; ++k) for (uint32_t i = 0; i < 256; ++i) crc_tab[k][i] = (crc_tab[k-1][i] >> 8) ^ crc_tab[0][crc_tab[k-1][i] & 0xFFu];
    crc_tab_ready = 1;
}
static uint32_t crc32_slice8(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n >= 8) {
        uint32_t lo, hi; memcpy(&lo, p, 4); memcpy(&hi, p + 4, 4);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        lo = __builtin_bswap32(lo); hi = __builtin_bswap32(hi);
#endif
        lo ^= c;
        c = crc_tab[7][lo & 0xFF] ^ crc_tab[6][(lo >> 8) & 0xFF] ^ crc_tab[5][(lo >> 16) & 0xFF] ^ crc_tab[4][lo >> 24]
          ^ crc_tab[3][hi & 0xFF] ^ crc_tab[2][(hi >> 8) & 0xFF] ^ crc_tab[1][(hi >> 16) & 0xFF] ^ crc_tab[0][hi >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = crc_tab[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

static uint32_t s_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = crc_tab[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

/* ------------------------------------------------------------------ */
/* vector tiers                                                        */
/* ------------------------------------------------------------------ */
#define MXS_VW 16
#define MXS_SFX _v128
#define MXS_TGT
#include "mx_superops_kernels.inc"
#undef MXS_VW
#undef MXS_SFX
#undef MXS_TGT
#if defined(__x86_64__) || defined(_M_X64)
#define MXS_HAVE_V256 1
#define MXS_VW 32
#define MXS_SFX _v256
#define MXS_TGT __attribute__((target("avx2")))
#include "mx_superops_kernels.inc"
#undef MXS_VW
#undef MXS_SFX
#undef MXS_TGT
#else
#define MXS_HAVE_V256 0
#endif

typedef struct {
    void     (*fill32)(uint32_t *, uint32_t, size_t);
    void     (*fill8)(uint8_t *, uint8_t, size_t);
    void     (*copy)(uint8_t *, const uint8_t *, size_t);
    void     (*blend_alpha)(uint32_t *, const uint32_t *, size_t);
    void     (*add_sat_bytes)(uint8_t *, const uint8_t *, const uint8_t *, size_t);
    void     (*vadd_f32)(float *, const float *, const float *, size_t);
    void     (*vmul_f32)(float *, const float *, const float *, size_t);
    void     (*vfma_f32)(float *, const float *, const float *, size_t);
    float    (*reduce_sum_f32)(const float *, size_t);
    float    (*dot_f32)(const float *, const float *, size_t);
    void     (*minmax_u8)(const uint8_t *, size_t, uint8_t *, uint8_t *);
    uint64_t (*dirty_row)(uint32_t *, const uint32_t *, size_t, uint8_t *, uint64_t);
    void     (*tri_row)(uint32_t *, int64_t, int64_t, int64_t, const int64_t *, const int64_t *, int64_t, uint32_t);
    void     (*tri_row32)(uint32_t *, int64_t, int64_t, int64_t, const int64_t *, const int64_t *, int64_t, uint32_t);
    uint32_t (*crc32)(const uint8_t *, size_t);
} mxs_kernels;

static const mxs_kernels K_scalar = { s_fill32, s_fill8, s_copy, s_blend_alpha, s_add_sat_bytes, s_vadd_f32, s_vmul_f32, s_vfma_f32,
    s_reduce_sum_f32, s_dot_f32, s_minmax_u8, s_dirty_row, s_tri_row, s_tri_row, s_crc32 };
static const mxs_kernels K_v128 = { k_fill32_v128, k_fill8_v128, k_copy_v128, k_blend_alpha_v128, k_add_sat_bytes_v128, k_vadd_f32_v128, k_vmul_f32_v128, k_vfma_f32_v128,
    k_reduce_sum_f32_v128, k_dot_f32_v128, k_minmax_u8_v128, k_dirty_row_v128, k_tri_row_v128, k_tri_row32_v128, crc32_slice8 };
#if MXS_HAVE_V256
static const mxs_kernels K_v256 = { k_fill32_v256, k_fill8_v256, k_copy_v256, k_blend_alpha_v256, k_add_sat_bytes_v256, k_vadd_f32_v256, k_vmul_f32_v256, k_vfma_f32_v256,
    k_reduce_sum_f32_v256, k_dot_f32_v256, k_minmax_u8_v256, k_dirty_row_v256, k_tri_row_v256, k_tri_row32_v256, crc32_slice8 };
#endif

static mxs_tier g_tier = MXS_TIER_AUTO;
static uint32_t g_tri_min_span_override = 0; static bool g_tri_override = false;
uint32_t mxs_set_tri_simd_min_span(uint32_t px) { g_tri_min_span_override = px; g_tri_override = true; return px; }
void     mxs_reset_tri_simd_min_span(void) { g_tri_override = false; }
uint32_t mxs_get_tri_simd_min_span(void) {
    if (g_tri_override) return g_tri_min_span_override;
    return mxs_get_tier() == MXS_TIER_V256 ? MXS_TRI_SIMD_MIN_SPAN_V256 : MXS_TRI_SIMD_MIN_SPAN_V128;
}
static const mxs_kernels *g_K = &K_v128;

mxs_tier mxs_detect_tier(void) {
#if MXS_HAVE_V256
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) return MXS_TIER_V256;
#endif
    return MXS_TIER_V128;
}
mxs_tier mxs_set_tier(mxs_tier t) {
    crc_tab_init();
    if (t == MXS_TIER_AUTO) t = mxs_detect_tier();
    if (t == MXS_TIER_V256 && mxs_detect_tier() != MXS_TIER_V256) t = MXS_TIER_V128;
    switch (t) {
        case MXS_TIER_SCALAR: g_K = &K_scalar; break;
#if MXS_HAVE_V256
        case MXS_TIER_V256:   g_K = &K_v256; break;
#endif
        default: t = MXS_TIER_V128; g_K = &K_v128; break;
    }
    g_tier = t;
    return t;
}
mxs_tier mxs_get_tier(void) { if (g_tier == MXS_TIER_AUTO) mxs_set_tier(MXS_TIER_AUTO); return g_tier; }
static inline const mxs_kernels *K(void) { if (g_tier == MXS_TIER_AUTO) mxs_set_tier(MXS_TIER_AUTO); return g_K; }
const char *mxs_tier_name(mxs_tier t) { switch (t) { case MXS_TIER_SCALAR: return "scalar"; case MXS_TIER_V128: return "v128"; case MXS_TIER_V256: return "v256"; default: return "auto"; } }
const char *mxs_err_name(mxs_err e) {
    static const char *n[MXS_E__COUNT] = {"ok","arg","zero_len","oversize","align","pitch","surface","rect","overlap","mode"};
    return (unsigned)e < MXS_E__COUNT ? n[e] : "?";
}

/* ------------------------------------------------------------------ */
/* bounds checks                                                       */
/* ------------------------------------------------------------------ */
/* n elements of `elem` bytes from index 0 of a span of `len` elements */
static mxs_err check_span(const void *base, uint64_t len, uint64_t n, unsigned elem) {
    if (n == 0) return MXS_E_ZERO_LEN;
    if (n > MXS_MAX_ELEMS) return MXS_E_OVERSIZE;
    if (!base) return MXS_E_ARG;
    if (elem > 1 && (((uintptr_t)base) & (elem - 1))) return MXS_E_ALIGN;
    if (n > len) return MXS_E_OVERSIZE;
    return MXS_OK;
}
static bool ranges_overlap(const void *a, uint64_t abytes, const void *b, uint64_t bbytes) {
    uintptr_t a0 = (uintptr_t)a, a1 = a0 + (uintptr_t)abytes, b0 = (uintptr_t)b, b1 = b0 + (uintptr_t)bbytes;
    return a0 < b1 && b0 < a1;
}
/* dst/src for element-wise ops: exact aliasing allowed, partial overlap not */
static mxs_err check_alias(const void *d, const void *s, uint64_t bytes) {
    if (d == s) return MXS_OK;
    return ranges_overlap(d, bytes, s, bytes) ? MXS_E_OVERLAP : MXS_OK;
}
static mxs_err surface_check_raw(const void *base, uint64_t len, uint32_t w, uint32_t h, uint32_t pitch) {
    if (w == 0 || h == 0) return MXS_E_ZERO_LEN;
    if (pitch < w) return MXS_E_PITCH;
    uint64_t need = (uint64_t)(h - 1) * pitch + w;           /* < 2^64 */
    if (need > MXS_MAX_ELEMS) return MXS_E_OVERSIZE;
    if (!base) return MXS_E_ARG;
    if (((uintptr_t)base) & 3u) return MXS_E_ALIGN;
    if (need > len) return MXS_E_SURFACE;
    return MXS_OK;
}
mxs_err mxs_surface_check(const mxs_surface *s)   { if (!s) return MXS_E_ARG; return surface_check_raw(s->px.base, s->px.len, s->w, s->h, s->pitch); }
mxs_err mxs_csurface_check(const mxs_csurface *s) { if (!s) return MXS_E_ARG; return surface_check_raw(s->px.base, s->px.len, s->w, s->h, s->pitch); }
static bool rect_inside(const mxs_rect *r, uint32_t w, uint32_t h) {
    if (r->w <= 0 || r->h <= 0 || r->x < 0 || r->y < 0) return false;
    return (int64_t)r->x + r->w <= (int64_t)w && (int64_t)r->y + r->h <= (int64_t)h;
}
static uint64_t surface_bytes(uint32_t w, uint32_t h, uint32_t pitch) { return ((uint64_t)(h - 1) * pitch + w) * 4u; }

/* ------------------------------------------------------------------ */
/* pixel entry points                                                  */
/* ------------------------------------------------------------------ */
mxs_err mxs_fill_rect(mxs_surface *dst, const mxs_rect *r, uint32_t color) {
    mxs_err e = mxs_surface_check(dst); if (e) return e;
    if (!r) return MXS_E_ARG;
    if (r->w <= 0 || r->h <= 0) return MXS_E_ZERO_LEN;
    if (!rect_inside(r, dst->w, dst->h)) return MXS_E_RECT;
    uint32_t *px = (uint32_t *)dst->px.base;
    for (int32_t j = 0; j < r->h; ++j) K()->fill32(px + (size_t)(r->y + j) * dst->pitch + (size_t)r->x, color, (size_t)r->w);
    return MXS_OK;
}
mxs_err mxs_clear(mxs_surface *dst, uint32_t color) {
    mxs_err e = mxs_surface_check(dst); if (e) return e;
    uint32_t *px = (uint32_t *)dst->px.base;
    if (dst->pitch == dst->w) { K()->fill32(px, color, (size_t)dst->w * dst->h); return MXS_OK; }
    for (uint32_t j = 0; j < dst->h; ++j) K()->fill32(px + (size_t)j * dst->pitch, color, dst->w);
    return MXS_OK;
}
mxs_err mxs_blit(mxs_surface *dst, int32_t dx, int32_t dy, const mxs_csurface *src, const mxs_rect *sr, mxs_blend mode) {
    mxs_err e = mxs_surface_check(dst); if (e) return e;
    e = mxs_csurface_check(src); if (e) return e;
    if (!sr) return MXS_E_ARG;
    if ((unsigned)mode >= MXS_BLEND__COUNT) return MXS_E_MODE;
    if (sr->w <= 0 || sr->h <= 0) return MXS_E_ZERO_LEN;
    if (!rect_inside(sr, src->w, src->h)) return MXS_E_RECT;
    mxs_rect dr = { dx, dy, sr->w, sr->h };
    if (!rect_inside(&dr, dst->w, dst->h)) return MXS_E_RECT;
    if (ranges_overlap(dst->px.base, surface_bytes(dst->w, dst->h, dst->pitch), src->px.base, surface_bytes(src->w, src->h, src->pitch))) return MXS_E_OVERLAP;
    uint32_t *d = (uint32_t *)dst->px.base; const uint32_t *s = (const uint32_t *)src->px.base;
    for (int32_t j = 0; j < sr->h; ++j) {
        uint32_t *drow = d + (size_t)(dy + j) * dst->pitch + (size_t)dx;
        const uint32_t *srow = s + (size_t)(sr->y + j) * src->pitch + (size_t)sr->x;
        switch (mode) {
            case MXS_BLEND_COPY:  K()->copy((uint8_t *)drow, (const uint8_t *)srow, (size_t)sr->w * 4u); break;
            case MXS_BLEND_ALPHA: K()->blend_alpha(drow, srow, (size_t)sr->w); break;
            default:              K()->add_sat_bytes((uint8_t *)drow, (const uint8_t *)drow, (const uint8_t *)srow, (size_t)sr->w * 4u); break;
        }
    }
    return MXS_OK;
}
mxs_err mxs_line(mxs_surface *dst, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    mxs_err e = mxs_surface_check(dst); if (e) return e;
    uint32_t *px = (uint32_t *)dst->px.base;
    int64_t dx = x1 > x0 ? (int64_t)x1 - x0 : (int64_t)x0 - x1, sx = x0 < x1 ? 1 : -1;
    int64_t dy = -(y1 > y0 ? (int64_t)y1 - y0 : (int64_t)y0 - y1), sy = y0 < y1 ? 1 : -1;
    int64_t err = dx + dy, cx = x0, cy = y0;
    for (;;) {
        if (cx >= 0 && cy >= 0 && cx < (int64_t)dst->w && cy < (int64_t)dst->h) px[(size_t)cy * dst->pitch + (size_t)cx] = color;
        if (cx == x1 && cy == y1) break;
        int64_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
    return MXS_OK;
}
mxs_err mxs_triangle(mxs_surface *dst, const mxs_rect *clip, const int64_t x[3], const int64_t y[3], uint32_t color) {
    mxs_err e = mxs_surface_check(dst); if (e) return e;
    if (!clip || !x || !y) return MXS_E_ARG;
    if (clip->w <= 0 || clip->h <= 0) return MXS_E_ZERO_LEN;
    if (!rect_inside(clip, dst->w, dst->h)) return MXS_E_RECT;
    for (int i = 0; i < 3; ++i) if (x[i] < -(1ll << 40) || x[i] > (1ll << 40) || y[i] < -(1ll << 40) || y[i] > (1ll << 40)) return MXS_E_OVERSIZE; /* keeps products in int64 */
    int64_t minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
    for (int i = 1; i < 3; ++i) { if (x[i] < minx) minx = x[i]; if (x[i] > maxx) maxx = x[i]; if (y[i] < miny) miny = y[i]; if (y[i] > maxy) maxy = y[i]; }
    if (minx < clip->x) minx = clip->x;
    if (miny < clip->y) miny = clip->y;
    if (maxx > (int64_t)clip->x + clip->w - 1) maxx = (int64_t)clip->x + clip->w - 1;
    if (maxy > (int64_t)clip->y + clip->h - 1) maxy = (int64_t)clip->y + clip->h - 1;
    int64_t area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (area == 0 || minx > maxx || miny > maxy) return MXS_OK;
    uint32_t *px = (uint32_t *)dst->px.base;
    /* 32-bit lanes when every vertex-to-pixel difference is below 2^14 (so each product < 2^28
     * and every weight < 2^30): the vertices and the clipped box must lie within one 2^14 window. */
    int64_t vminx = x[0], vmaxx = x[0], vminy = y[0], vmaxy = y[0];
    for (int i = 1; i < 3; ++i) { if (x[i] < vminx) vminx = x[i]; if (x[i] > vmaxx) vmaxx = x[i]; if (y[i] < vminy) vminy = y[i]; if (y[i] > vmaxy) vmaxy = y[i]; }
    int64_t ex = (vmaxx > maxx ? vmaxx : maxx) - (vminx < minx ? vminx : minx);
    int64_t ey = (vmaxy > maxy ? vmaxy : maxy) - (vminy < miny ? vminy : miny);
    bool small = ex < (1 << 14) && ey < (1 << 14);
    bool use_simd = (uint64_t)(maxx - minx + 1) >= (uint64_t)mxs_get_tri_simd_min_span();   /* short rows: scalar wins (see README) */
    const mxs_kernels *k = use_simd ? K() : &K_scalar;
    for (int64_t py = miny; py <= maxy; ++py) {
        uint32_t *row = px + (size_t)py * dst->pitch;
        if (small) k->tri_row32(row, minx, maxx, py, x, y, area, color);
        else       k->tri_row(row, minx, maxx, py, x, y, area, color);
    }
    return MXS_OK;
}
mxs_err mxs_blit_dirty(mxs_surface *dst, int32_t dx, int32_t dy, const mxs_csurface *src, const mxs_rect *sr, mxs_span shadow, uint64_t *changed) {
    if (!changed) return MXS_E_ARG;
    *changed = 0;
    mxs_err e = mxs_surface_check(dst); if (e) return e;
    e = mxs_csurface_check(src); if (e) return e;
    if (!sr) return MXS_E_ARG;
    if (sr->w <= 0 || sr->h <= 0) return MXS_E_ZERO_LEN;
    if (!rect_inside(sr, src->w, src->h)) return MXS_E_RECT;
    mxs_rect dr = { dx, dy, sr->w, sr->h };
    if (!rect_inside(&dr, dst->w, dst->h)) return MXS_E_RECT;
    uint64_t dbytes = surface_bytes(dst->w, dst->h, dst->pitch), sbytes = surface_bytes(src->w, src->h, src->pitch);
    if (ranges_overlap(dst->px.base, dbytes, src->px.base, sbytes)) return MXS_E_OVERLAP;
    /* shadow must hold one bit per dst pixel index up to the last touched pixel */
    uint64_t last_bit = (uint64_t)(dy + sr->h - 1) * dst->pitch + (uint64_t)(dx + sr->w - 1);
    uint64_t need = last_bit / 8u + 1u;
    if (!shadow.base) return MXS_E_ARG;
    if (need > shadow.len) return MXS_E_OVERSIZE;
    if (ranges_overlap(shadow.base, shadow.len, dst->px.base, dbytes) || ranges_overlap(shadow.base, shadow.len, src->px.base, sbytes)) return MXS_E_OVERLAP;
    uint32_t *d = (uint32_t *)dst->px.base; const uint32_t *s = (const uint32_t *)src->px.base; uint8_t *sh = (uint8_t *)shadow.base;
    uint64_t ch = 0;
    for (int32_t j = 0; j < sr->h; ++j) {
        uint64_t bit0 = (uint64_t)(dy + j) * dst->pitch + (uint64_t)dx;
        ch += K()->dirty_row(d + bit0, s + (size_t)(sr->y + j) * src->pitch + (size_t)sr->x, (size_t)sr->w, sh, bit0);
    }
    *changed = ch;
    return MXS_OK;
}

/* ------------------------------------------------------------------ */
/* vector entry points                                                 */
/* ------------------------------------------------------------------ */
#define CHECK3(dst, a, b, n, elem) do { \
    mxs_err _e; \
    if ((_e = check_span((dst).base, (dst).len, (n), (elem)))) return _e; \
    if ((_e = check_span((a).base, (a).len, (n), (elem)))) return _e; \
    if ((_e = check_span((b).base, (b).len, (n), (elem)))) return _e; \
    if ((_e = check_alias((dst).base, (a).base, (n) * (elem)))) return _e; \
    if ((_e = check_alias((dst).base, (b).base, (n) * (elem)))) return _e; \
} while (0)

mxs_err mxs_vadd_f32(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n) { CHECK3(dst, a, b, n, 4); K()->vadd_f32((float *)dst.base, (const float *)a.base, (const float *)b.base, (size_t)n); return MXS_OK; }
mxs_err mxs_vmul_f32(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n) { CHECK3(dst, a, b, n, 4); K()->vmul_f32((float *)dst.base, (const float *)a.base, (const float *)b.base, (size_t)n); return MXS_OK; }
mxs_err mxs_vfma_f32(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n) { CHECK3(dst, a, b, n, 4); K()->vfma_f32((float *)dst.base, (const float *)a.base, (const float *)b.base, (size_t)n); return MXS_OK; }
mxs_err mxs_vadd_sat_u8(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n) { CHECK3(dst, a, b, n, 1); K()->add_sat_bytes((uint8_t *)dst.base, (const uint8_t *)a.base, (const uint8_t *)b.base, (size_t)n); return MXS_OK; }
mxs_err mxs_fill8(mxs_span dst, uint8_t v, uint64_t n) { mxs_err e = check_span(dst.base, dst.len, n, 1); if (e) return e; K()->fill8((uint8_t *)dst.base, v, (size_t)n); return MXS_OK; }
mxs_err mxs_copy(mxs_span dst, mxs_cspan src, uint64_t n) {
    mxs_err e; if ((e = check_span(dst.base, dst.len, n, 1))) return e; if ((e = check_span(src.base, src.len, n, 1))) return e;
    if (ranges_overlap(dst.base, n, src.base, n)) return MXS_E_OVERLAP;
    K()->copy((uint8_t *)dst.base, (const uint8_t *)src.base, (size_t)n); return MXS_OK;
}
mxs_err mxs_crc32(mxs_cspan src, uint64_t n, uint32_t *out) { if (!out) return MXS_E_ARG; mxs_err e = check_span(src.base, src.len, n, 1); if (e) return e; *out = K()->crc32((const uint8_t *)src.base, (size_t)n); return MXS_OK; }
mxs_err mxs_reduce_sum_f32(mxs_cspan a, uint64_t n, float *out) { if (!out) return MXS_E_ARG; mxs_err e = check_span(a.base, a.len, n, 4); if (e) return e; *out = K()->reduce_sum_f32((const float *)a.base, (size_t)n); return MXS_OK; }
mxs_err mxs_dot_f32(mxs_cspan a, mxs_cspan b, uint64_t n, float *out) {
    if (!out) return MXS_E_ARG;
    mxs_err e;
    if ((e = check_span(a.base, a.len, n, 4))) return e;
    if ((e = check_span(b.base, b.len, n, 4))) return e;
    *out = K()->dot_f32((const float *)a.base, (const float *)b.base, (size_t)n); return MXS_OK;
}
mxs_err mxs_minmax_u8(mxs_cspan a, uint64_t n, uint8_t *mn, uint8_t *mx) { if (!mn || !mx) return MXS_E_ARG; mxs_err e = check_span(a.base, a.len, n, 1); if (e) return e; K()->minmax_u8((const uint8_t *)a.base, (size_t)n, mn, mx); return MXS_OK; }
