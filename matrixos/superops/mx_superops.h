/*
 * mx_superops.h - MatrixOS SIMD super-op kernels (wave 1, matrixos/superops/)
 *
 * This is the kernel layer underneath two consumers:
 *   1. the render service command set (contracts/gpu-commands-ref/rcs.h): its
 *      executor calls these for FILL_RECT, BLIT (COPY / ALPHA / ADD), CLEAR,
 *      LINE and DRAW_TRIANGLES instead of its scalar loops; pixel results are
 *      bit-identical to the reference executor in rcs.c;
 *   2. the compiler's vector lowering (mx_superops_lower.h): countable loops
 *      over f32 / u8 arrays lower onto the mxs_v* entry points.
 *
 * Contract (security review finding 6):
 *   Every entry point takes BOUNDED buffers (mxs_span: base + length in
 *   elements). Geometry, counts and offsets are checked against those lengths
 *   in 64-bit, overflow-safe arithmetic before any kernel touches memory.
 *   Zero-length and oversize requests are rejected with a named error.
 *   Nothing is written when an entry point returns an error.
 *
 * Determinism:
 *   Every kernel has a scalar reference. The SIMD tiers (128-bit baseline;
 *   256-bit AVX2 on x86-64) produce bit-identical output, including the f32
 *   reductions, which use a fixed 8-lane strided accumulation order. Tests
 *   compare every tier against the reference on random inputs.
 */
#ifndef MX_SUPEROPS_H
#define MX_SUPEROPS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MXS_VERSION      1u
#define MXS_MAX_ELEMS    (1ull << 31)     /* per call; matches RCS pixel budgets with room */

/* A bounded buffer: `len` elements of `elem` bytes each at `base`. */
typedef struct {
    void    *base;
    uint64_t len;     /* in elements */
} mxs_span;

typedef struct {
    const void *base;
    uint64_t    len;
} mxs_cspan;

static inline mxs_span  mxs_span_of(void *p, uint64_t n)        { mxs_span s = {p, n}; return s; }
static inline mxs_cspan mxs_cspan_of(const void *p, uint64_t n) { mxs_cspan s = {p, n}; return s; }

/* A pixel surface view: w x h pixels (uint32_t each) inside a span, with a row pitch. */
typedef struct {
    mxs_span px;      /* backing buffer, uint32_t elements */
    uint32_t w, h;    /* logical size */
    uint32_t pitch;   /* elements per row, >= w */
} mxs_surface;

typedef struct {
    mxs_cspan px;
    uint32_t  w, h, pitch;
} mxs_csurface;

typedef struct { int32_t x, y, w, h; } mxs_rect;   /* same layout as rcs_rect */

typedef enum {
    MXS_OK = 0,
    MXS_E_ARG,        /* null span base with nonzero len, null output          */
    MXS_E_ZERO_LEN,   /* count == 0 or rect with w <= 0 or h <= 0               */
    MXS_E_OVERSIZE,   /* count > MXS_MAX_ELEMS, or request exceeds the span     */
    MXS_E_ALIGN,      /* base not aligned to the element size                   */
    MXS_E_PITCH,      /* pitch < w                                              */
    MXS_E_SURFACE,    /* w*h via pitch does not fit the backing span            */
    MXS_E_RECT,       /* rect not inside its surface                            */
    MXS_E_OVERLAP,    /* dst overlaps a source other than exactly in place      */
    MXS_E_MODE,       /* unknown blend mode / shading                           */
    MXS_E__COUNT
} mxs_err;
const char *mxs_err_name(mxs_err e);

/* Blend modes, numerically equal to rcs_blend. */
typedef enum { MXS_BLEND_COPY = 0, MXS_BLEND_ALPHA = 1, MXS_BLEND_ADD = 2, MXS_BLEND__COUNT = 3 } mxs_blend;

/* SIMD tier selection. Global, set once at startup (or per test). */
typedef enum { MXS_TIER_AUTO = 0, MXS_TIER_SCALAR = 1, MXS_TIER_V128 = 2, MXS_TIER_V256 = 3 } mxs_tier;
mxs_tier    mxs_detect_tier(void);          /* best tier this CPU supports */
mxs_tier    mxs_set_tier(mxs_tier t);       /* returns the tier actually selected */
mxs_tier    mxs_get_tier(void);
const char *mxs_tier_name(mxs_tier t);

/* ---------------- pixel kernels (render service) ---------------- */

/* Validate a surface view against its span. Used by every pixel entry point. */
mxs_err mxs_surface_check(const mxs_surface *s);
mxs_err mxs_csurface_check(const mxs_csurface *s);

/* Fill rect r of dst with color. r must lie inside dst (caller clips first, as rcs.c does). */
mxs_err mxs_fill_rect(mxs_surface *dst, const mxs_rect *r, uint32_t color);
/* Whole-surface clear. */
mxs_err mxs_clear(mxs_surface *dst, uint32_t color);
/* Blit src rect sr onto dst at (dx,dy) with blend mode. Both rects must be inside their
 * surfaces; dst and src must not overlap in memory. Pixel formula == rcs.c blend_px. */
mxs_err mxs_blit(mxs_surface *dst, int32_t dx, int32_t dy, const mxs_csurface *src, const mxs_rect *sr, mxs_blend mode);
/* Bresenham line, clipped to dst. Scalar only (not a SIMD shape); bounded like the rest. */
mxs_err mxs_line(mxs_surface *dst, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
/* Triangle rasteriser path choice. Rows shorter than this many pixels run the scalar
 * incremental kernel even on a SIMD tier, because the per-row vector setup costs more
 * than it saves on short spans (measured: see README "Triangle span threshold"). 0 forces
 * SIMD on every row, UINT32_MAX forces scalar; returns the value in effect. */
uint32_t mxs_set_tri_simd_min_span(uint32_t px);     /* override for every tier */
void     mxs_reset_tri_simd_min_span(void);         /* back to the per-tier defaults below */
uint32_t mxs_get_tri_simd_min_span(void);           /* value in effect for the current tier */
#define MXS_TRI_SIMD_MIN_SPAN_V128 128u              /* SSE2/NEON: wins clearly from 128 px, marginal at 96 */
#define MXS_TRI_SIMD_MIN_SPAN_V256 96u               /* AVX2: wins from 96 px */
#define MXS_TRI_SIMD_MIN_SPAN_DEFAULT MXS_TRI_SIMD_MIN_SPAN_V256   /* kept for older callers */

/* Flat-color triangle, integer pixel vertices, clipped to `clip` (inside dst). Same inside
 * test and fill convention as rcs.c DRAW_TRIANGLES. Rows are rasterised with SIMD. */
mxs_err mxs_triangle(mxs_surface *dst, const mxs_rect *clip, const int64_t x[3], const int64_t y[3], uint32_t color);
/* Copy src rect onto dst and report which dst pixels changed: toggles one bit per changed
 * dst pixel in `shadow` (bit index = dst pixel index, i.e. y*pitch+x) and returns the
 * count in *changed. Damage tracking for the ghost canvas. shadow.len is in bytes. */
mxs_err mxs_blit_dirty(mxs_surface *dst, int32_t dx, int32_t dy, const mxs_csurface *src, const mxs_rect *sr, mxs_span shadow, uint64_t *changed);

/* ---------------- vector kernels (compiler lowering targets) ---------------- */
/* All take element counts; spans are checked for n elements from index 0.
 * dst may equal src exactly (same base) for the element-wise ops; partial overlap is rejected. */
mxs_err mxs_vadd_f32(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n);       /* d[i] = a[i] + b[i]        */
mxs_err mxs_vmul_f32(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n);       /* d[i] = a[i] * b[i]        */
mxs_err mxs_vfma_f32(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n);       /* d[i] = a[i]*b[i] + d[i], two roundings */
mxs_err mxs_vadd_sat_u8(mxs_span dst, mxs_cspan a, mxs_cspan b, uint64_t n);    /* d[i] = min(a[i]+b[i],255) */
mxs_err mxs_fill8(mxs_span dst, uint8_t v, uint64_t n);
mxs_err mxs_copy(mxs_span dst, mxs_cspan src, uint64_t n);                      /* bytes; no overlap          */
mxs_err mxs_crc32(mxs_cspan src, uint64_t n, uint32_t *out);                    /* IEEE CRC-32 (zlib)        */
mxs_err mxs_reduce_sum_f32(mxs_cspan a, uint64_t n, float *out);                /* 8-lane strided order      */
mxs_err mxs_dot_f32(mxs_cspan a, mxs_cspan b, uint64_t n, float *out);
mxs_err mxs_minmax_u8(mxs_cspan a, uint64_t n, uint8_t *min_out, uint8_t *max_out);

/* Pixel-op charge per gpu-commands.md section 2: one destination pixel written with at most one
 * source read is 1 op; ALPHA and ADD read the destination too and charge 2. Kernels never charge
 * (the executor's validator does); this keeps the rule in one place for both. */
static inline uint64_t mxs_pixel_ops(mxs_blend mode, uint32_t w, uint32_t h) { return (uint64_t)w * h * (mode == MXS_BLEND_COPY ? 1u : 2u); }

/* ---------------- scalar references (exported for tests) ---------------- */
uint32_t mxs_ref_blend_px(uint32_t d, uint32_t s, mxs_blend mode);
uint32_t mxs_ref_crc32(const uint8_t *p, size_t n);
void     mxs_ref_vadd_f32(float *d, const float *a, const float *b, size_t n);
void     mxs_ref_vmul_f32(float *d, const float *a, const float *b, size_t n);
void     mxs_ref_vfma_f32(float *d, const float *a, const float *b, size_t n);
void     mxs_ref_vadd_sat_u8(uint8_t *d, const uint8_t *a, const uint8_t *b, size_t n);
float    mxs_ref_reduce_sum_f32(const float *a, size_t n);
float    mxs_ref_dot_f32(const float *a, const float *b, size_t n);
void     mxs_ref_minmax_u8(const uint8_t *a, size_t n, uint8_t *mn, uint8_t *mx);
void     mxs_ref_tri_row(uint32_t *row, int64_t px0, int64_t px1, int64_t py, const int64_t x[3], const int64_t y[3], int64_t area, uint32_t color);

#ifdef __cplusplus
}
#endif
#endif
