/*
 * mx_superops_lower.h - compiler vector lowering onto the super-op kernels.
 *
 * Header-only. The codegen thread hands in a normalised description of a
 * countable loop (induction variable 0..n-1, unit stride, one statement) and
 * gets back either a kernel to call or a reason the loop stays scalar. The
 * lowering is only legal when the kernel's own bounded contract will accept
 * the call, so the same aliasing rules apply here (exact alias of dst with a
 * source is fine, partial overlap is not), and the array lengths the frontend
 * knows become the spans the kernel checks at run time.
 *
 * Fixed SIMD tiers (CONTROL.md decision 3): mxs_tier_cflags() gives the flags
 * the build profile adds per tier instead of -march=native.
 */
#ifndef MX_SUPEROPS_LOWER_H
#define MX_SUPEROPS_LOWER_H
#include "mx_superops.h"

typedef enum { MXL_F32 = 0, MXL_U8 = 1 } mxl_type;

/* The loop body, with arrays named by small ids (0..3) the caller assigns. -1 = none. */
typedef enum {
    MXL_BODY_STORE_CONST,   /* dst[i] = imm                      -> fill8 (u8 only)       */
    MXL_BODY_STORE_COPY,    /* dst[i] = a[i]                     -> copy                  */
    MXL_BODY_ADD,           /* dst[i] = a[i] + b[i]              -> vadd_f32              */
    MXL_BODY_MUL,           /* dst[i] = a[i] * b[i]              -> vmul_f32              */
    MXL_BODY_MUL_ADD_DST,   /* dst[i] = a[i] * b[i] + dst[i]     -> vfma_f32 (two roundings; the C semantics without -ffp-contract) */
    MXL_BODY_ADD_SAT,       /* dst[i] = min(a[i] + b[i], 255)    -> vadd_sat_u8           */
    MXL_BODY_REDUCE_SUM,    /* acc += a[i]                       -> reduce_sum_f32 (see note on order) */
    MXL_BODY_REDUCE_DOT,    /* acc += a[i] * b[i]                -> dot_f32              */
    MXL_BODY_REDUCE_MINMAX, /* mn = min(mn,a[i]); mx = max(mx,a[i]) -> minmax_u8          */
    MXL_BODY_CRC32,         /* crc = crc32_update(crc, a[i])     -> crc32 (whole buffer)  */
    MXL_BODY__COUNT
} mxl_body;

typedef struct {
    mxl_type elem;
    mxl_body body;
    int      dst, a, b;          /* array ids; -1 if absent */
    uint64_t imm;                /* STORE_CONST value */
    bool     fast_math;          /* source was compiled with reassociation allowed (-ffast-math / -fassociative-math) */
    bool     n_known;            /* trip count known at compile time */
    uint64_t n;
    uint64_t len[4];             /* known element counts of arrays 0..3 (0 = unknown) */
    int8_t   alias[4][4];        /* alias[i][j]: 0 provably disjoint, 1 exactly the same array, 2 may partially overlap or unknown */
} mxl_loop;

typedef enum {
    MXL_OK = 0,
    MXL_SKIP_BODY,          /* no kernel for this body shape                               */
    MXL_SKIP_TYPE,          /* body/type combination not provided (e.g. u8 add without saturation) */
    MXL_SKIP_ALIAS,         /* dst may partially overlap a source                           */
    MXL_SKIP_ORDER,         /* f32 reduction without fast-math: the kernel's 8-lane order differs from the source's sequential sum */
    MXL_SKIP_LEN,           /* trip count exceeds a known array length (would fault at run time anyway, keep the scalar loop and its diagnostic) */
    MXL_SKIP_ARG            /* malformed description                                        */
} mxl_status;

typedef enum {
    MXL_K_NONE = 0, MXL_K_FILL8, MXL_K_COPY, MXL_K_VADD_F32, MXL_K_VMUL_F32, MXL_K_VFMA_F32, MXL_K_VADD_SAT_U8,
    MXL_K_REDUCE_SUM_F32, MXL_K_DOT_F32, MXL_K_MINMAX_U8, MXL_K_CRC32
} mxl_kernel;

typedef struct {
    mxl_status status;
    mxl_kernel kernel;
    const char *symbol;     /* C symbol the codegen emits a call to */
    unsigned    elem_bytes;
    bool        in_place;   /* dst is the same array as a source: emit dst twice */
} mxl_lowered;

static inline const char *mxl_status_name(mxl_status s) {
    static const char *n[] = {"ok","skip_body","skip_type","skip_alias","skip_order","skip_len","skip_arg"};
    return (unsigned)s < 7 ? n[s] : "?";
}

static inline mxl_lowered mxl_lower(const mxl_loop *L) {
    mxl_lowered out = { MXL_OK, MXL_K_NONE, NULL, 0, false };
#define SKIP(s) do { out.status = (s); out.kernel = MXL_K_NONE; out.symbol = NULL; return out; } while (0)
    if (!L || (unsigned)L->body >= MXL_BODY__COUNT) SKIP(MXL_SKIP_ARG);
    if (L->dst > 3 || L->a > 3 || L->b > 3) SKIP(MXL_SKIP_ARG);
    bool f32 = L->elem == MXL_F32;
    out.elem_bytes = f32 ? 4u : 1u;
    bool needs_dst = true, needs_a = true, needs_b = false;
    switch (L->body) {
        case MXL_BODY_STORE_CONST:   if (f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_FILL8; out.symbol = "mxs_fill8"; needs_a = false; break;
        case MXL_BODY_STORE_COPY:    out.kernel = MXL_K_COPY; out.symbol = "mxs_copy"; break;
        case MXL_BODY_ADD:           if (!f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_VADD_F32; out.symbol = "mxs_vadd_f32"; needs_b = true; break;
        case MXL_BODY_MUL:           if (!f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_VMUL_F32; out.symbol = "mxs_vmul_f32"; needs_b = true; break;
        case MXL_BODY_MUL_ADD_DST:   if (!f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_VFMA_F32; out.symbol = "mxs_vfma_f32"; needs_b = true; break;
        case MXL_BODY_ADD_SAT:       if (f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_VADD_SAT_U8; out.symbol = "mxs_vadd_sat_u8"; needs_b = true; break;
        case MXL_BODY_REDUCE_SUM:    if (!f32) SKIP(MXL_SKIP_TYPE); if (!L->fast_math) SKIP(MXL_SKIP_ORDER); out.kernel = MXL_K_REDUCE_SUM_F32; out.symbol = "mxs_reduce_sum_f32"; needs_dst = false; break;
        case MXL_BODY_REDUCE_DOT:    if (!f32) SKIP(MXL_SKIP_TYPE); if (!L->fast_math) SKIP(MXL_SKIP_ORDER); out.kernel = MXL_K_DOT_F32; out.symbol = "mxs_dot_f32"; needs_dst = false; needs_b = true; break;
        case MXL_BODY_REDUCE_MINMAX: if (f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_MINMAX_U8; out.symbol = "mxs_minmax_u8"; needs_dst = false; break;
        case MXL_BODY_CRC32:         if (f32) SKIP(MXL_SKIP_TYPE); out.kernel = MXL_K_CRC32; out.symbol = "mxs_crc32"; needs_dst = false; break;
        default: SKIP(MXL_SKIP_BODY);
    }
    if (needs_dst && L->dst < 0) SKIP(MXL_SKIP_ARG);
    if (needs_a && L->a < 0) SKIP(MXL_SKIP_ARG);
    if (needs_b && L->b < 0) SKIP(MXL_SKIP_ARG);
    /* aliasing: dst vs each source. Exact alias allowed for element-wise ops, never for copy. */
    if (needs_dst) {
        int srcs[2] = { needs_a ? L->a : -1, needs_b ? L->b : -1 };
        for (int k = 0; k < 2; ++k) {
            if (srcs[k] < 0) continue;
            int8_t al = L->alias[L->dst][srcs[k]];
            if (al == 2) SKIP(MXL_SKIP_ALIAS);
            if (al == 1) { if (L->body == MXL_BODY_STORE_COPY) SKIP(MXL_SKIP_ALIAS); out.in_place = true; }
        }
    }
    /* known trip count vs known lengths */
    if (L->n_known) {
        int ids[3] = { needs_dst ? L->dst : -1, needs_a ? L->a : -1, needs_b ? L->b : -1 };
        for (int k = 0; k < 3; ++k) if (ids[k] >= 0 && L->len[ids[k]] && L->n > L->len[ids[k]]) SKIP(MXL_SKIP_LEN);
    }
#undef SKIP
    return out;
}

/* Build profile flags per fixed tier, x86-64. No -march=native anywhere. -ffp-contract=off keeps
 * a*b+c as two roundings everywhere so the lowered VFMA and the scalar C agree bit for bit. */
static inline const char *mxs_tier_cflags(mxs_tier t) {
    switch (t) {
        case MXS_TIER_SCALAR: return "-fno-tree-vectorize -ffp-contract=off";
        case MXS_TIER_V128:   return "-msse2 -ffp-contract=off";              /* x86-64 baseline; NEON is implicit on aarch64 */
        case MXS_TIER_V256:   return "-mavx2 -mno-fma -ffp-contract=off";     /* kernels carry their own target attribute; this tier flag is for the rest of the program */
        default:              return "-msse2 -ffp-contract=off";
    }
}
#endif
