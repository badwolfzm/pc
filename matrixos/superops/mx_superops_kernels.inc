/*
 * Vector kernel bodies, included once per SIMD tier with
 *   MXS_VW  vector width in bytes (16 or 32)
 *   MXS_SFX name suffix
 *   MXS_TGT function attribute (e.g. __attribute__((target("avx2")))) or empty
 * GCC/clang vector extensions only, so one source serves SSE2, AVX2 and NEON.
 * Each kernel is bit-identical to its scalar reference in mx_superops.c.
 */
#define MXS_CAT_(a,b) a##b
#define MXS_CAT(a,b) MXS_CAT_(a,b)
#define KFN(name) MXS_CAT(name, MXS_SFX)
#define NF (MXS_VW / 4)
#define NB (MXS_VW)
#define NQ (MXS_VW / 8)

typedef float    KFN(vf_)  __attribute__((vector_size(MXS_VW)));
typedef uint32_t KFN(vu_)  __attribute__((vector_size(MXS_VW)));
typedef int32_t  KFN(vi_)  __attribute__((vector_size(MXS_VW)));
typedef uint8_t  KFN(vb_)  __attribute__((vector_size(MXS_VW)));
typedef int8_t   KFN(vsb_) __attribute__((vector_size(MXS_VW)));
typedef int64_t  KFN(vq_)  __attribute__((vector_size(MXS_VW)));
#define vf  KFN(vf_)
#define vu  KFN(vu_)
#define vi  KFN(vi_)
#define vb  KFN(vb_)
#define vsb KFN(vsb_)
#define vq  KFN(vq_)
#define LD(T, p) ({ T _t; memcpy(&_t, (p), sizeof _t); _t; })
#define ST(p, v) memcpy((p), &(v), sizeof(v))

MXS_TGT static void KFN(k_fill32)(uint32_t *d, uint32_t c, size_t n) {
    size_t i = 0; vu v; for (int k = 0; k < NF; ++k) v[k] = c;
    for (; i + NF <= n; i += NF) ST(d + i, v);
    for (; i < n; ++i) d[i] = c;
}
MXS_TGT static void KFN(k_fill8)(uint8_t *d, uint8_t c, size_t n) {
    size_t i = 0; vb v; for (int k = 0; k < NB; ++k) v[k] = c;
    for (; i + NB <= n; i += NB) ST(d + i, v);
    for (; i < n; ++i) d[i] = c;
}
MXS_TGT static void KFN(k_copy)(uint8_t *d, const uint8_t *s, size_t n) {
    size_t i = 0;
    for (; i + NB <= n; i += NB) { vb v = LD(vb, s + i); ST(d + i, v); }
    for (; i < n; ++i) d[i] = s[i];
}
/* ALPHA: gpu-commands.md normative rule. t = s*a + d*(255-a) + 128; out = (t + (t >> 8)) >> 8; dst alpha kept. */
MXS_TGT static void KFN(k_blend_alpha)(uint32_t *d, const uint32_t *s, size_t n) {
    size_t i = 0;
    for (; i + NF <= n; i += NF) {
        vu S = LD(vu, s + i), D = LD(vu, d + i);
        vu sa = S >> 24, ia = 255u - sa;
        vu b = (S & 0xFFu) * sa + (D & 0xFFu) * ia + 128u;
        vu g = ((S >> 8) & 0xFFu) * sa + ((D >> 8) & 0xFFu) * ia + 128u;
        vu r = ((S >> 16) & 0xFFu) * sa + ((D >> 16) & 0xFFu) * ia + 128u;
        b = (b + (b >> 8)) >> 8; g = (g + (g >> 8)) >> 8; r = (r + (r >> 8)) >> 8;
        vu out = (D & 0xFF000000u) | (r << 16) | (g << 8) | b;
        ST(d + i, out);
    }
    for (; i < n; ++i) d[i] = blend_alpha_px(d[i], s[i]);
}
/* ADD: per-byte saturating add, all four channels */
MXS_TGT static void KFN(k_add_sat_bytes)(uint8_t *d, const uint8_t *a, const uint8_t *b, size_t n) {
    size_t i = 0;
    for (; i + NB <= n; i += NB) {
        vb x = LD(vb, a + i), y = LD(vb, b + i);
        vb s = x + y;
        vsb wrap = (vsb)(s < x);
        vb r = s | (vb)wrap;
        ST(d + i, r);
    }
    for (; i < n; ++i) { unsigned s = a[i] + b[i]; d[i] = (uint8_t)(s > 255u ? 255u : s); }
}
MXS_TGT static void KFN(k_vadd_f32)(float *d, const float *a, const float *b, size_t n) {
    size_t i = 0;
    for (; i + NF <= n; i += NF) { vf x = LD(vf, a + i), y = LD(vf, b + i); vf r = x + y; ST(d + i, r); }
    for (; i < n; ++i) d[i] = a[i] + b[i];
}
MXS_TGT static void KFN(k_vmul_f32)(float *d, const float *a, const float *b, size_t n) {
    size_t i = 0;
    for (; i + NF <= n; i += NF) { vf x = LD(vf, a + i), y = LD(vf, b + i); vf r = x * y; ST(d + i, r); }
    for (; i < n; ++i) d[i] = a[i] * b[i];
}
MXS_TGT static void KFN(k_vfma_f32)(float *d, const float *a, const float *b, size_t n) {
    size_t i = 0;
    for (; i + NF <= n; i += NF) { vf x = LD(vf, a + i), y = LD(vf, b + i), z = LD(vf, d + i); vf p = x * y; vf r = p + z; ST(d + i, r); }
    for (; i < n; ++i) { float p = a[i] * b[i]; d[i] = p + d[i]; }
}
MXS_TGT static float KFN(k_reduce_sum_f32)(const float *a, size_t n) {
    float lane[8] = {0}; size_t i = 0;
#if NF == 8
    vf acc = {0};
    for (; i + 8 <= n; i += 8) { vf x = LD(vf, a + i); acc = acc + x; }
    ST(lane, acc);
#else
    vf acc0 = {0}, acc1 = {0};
    for (; i + 8 <= n; i += 8) { vf x = LD(vf, a + i), y = LD(vf, a + i + 4); acc0 = acc0 + x; acc1 = acc1 + y; }
    ST(lane, acc0); ST(lane + 4, acc1);
#endif
    for (; i < n; ++i) lane[i & 7] += a[i];
    return mxs_hsum8(lane);
}
MXS_TGT static float KFN(k_dot_f32)(const float *a, const float *b, size_t n) {
    float lane[8] = {0}; size_t i = 0;
#if NF == 8
    vf acc = {0};
    for (; i + 8 <= n; i += 8) { vf x = LD(vf, a + i), y = LD(vf, b + i); vf p = x * y; acc = acc + p; }
    ST(lane, acc);
#else
    vf acc0 = {0}, acc1 = {0};
    for (; i + 8 <= n; i += 8) {
        vf x0 = LD(vf, a + i), y0 = LD(vf, b + i), x1 = LD(vf, a + i + 4), y1 = LD(vf, b + i + 4);
        vf p0 = x0 * y0, p1 = x1 * y1; acc0 = acc0 + p0; acc1 = acc1 + p1;
    }
    ST(lane, acc0); ST(lane + 4, acc1);
#endif
    for (; i < n; ++i) { float p = a[i] * b[i]; lane[i & 7] += p; }
    return mxs_hsum8(lane);
}
MXS_TGT static void KFN(k_minmax_u8)(const uint8_t *a, size_t n, uint8_t *mn_out, uint8_t *mx_out) {
    size_t i = 0; uint8_t mn = 255, mx = 0;
    if (n >= (size_t)NB) {
        vb vmn = LD(vb, a), vmx = vmn; i = NB;
        for (; i + NB <= n; i += NB) {
            vb x = LD(vb, a + i);
            vsb lt = (vsb)(x < vmn), gt = (vsb)(x > vmx);
            vmn = (x & (vb)lt) | (vmn & ~(vb)lt);
            vmx = (x & (vb)gt) | (vmx & ~(vb)gt);
        }
        for (int k = 0; k < NB; ++k) { if (vmn[k] < mn) mn = vmn[k]; if (vmx[k] > mx) mx = vmx[k]; }
    }
    for (; i < n; ++i) { if (a[i] < mn) mn = a[i]; if (a[i] > mx) mx = a[i]; }
    *mn_out = mn; *mx_out = mx;
}
/* copy one row, toggle a shadow bit per changed pixel, count changes */
MXS_TGT static uint64_t KFN(k_dirty_row)(uint32_t *d, const uint32_t *s, size_t w, uint8_t *shadow, uint64_t bit0) {
    uint64_t changed = 0; size_t i = 0;
    for (; i + NF <= w; i += NF) {
        vu x = LD(vu, s + i), y = LD(vu, d + i);
        vi ne = (vi)(x != y);
        int32_t any = 0; for (int k = 0; k < NF; ++k) any |= ne[k];
        if (any) for (int k = 0; k < NF; ++k) if (ne[k]) { uint64_t bit = bit0 + i + (uint64_t)k; shadow[bit >> 3] ^= (uint8_t)(1u << (bit & 7)); changed++; }
        ST(d + i, x);
    }
    for (; i < w; ++i) { if (d[i] != s[i]) { uint64_t bit = bit0 + i; shadow[bit >> 3] ^= (uint8_t)(1u << (bit & 7)); changed++; } d[i] = s[i]; }
    return changed;
}
/* one triangle scanline: pixels px0..px1 inclusive on row py. Barycentric weights are
 * linear in px, so lanes step by constant deltas; integer math, identical to the reference. */
MXS_TGT static void KFN(k_tri_row)(uint32_t *row, int64_t px0, int64_t px1, int64_t py, const int64_t x[3], const int64_t y[3], int64_t area, uint32_t color) {
    int64_t w0 = (x[1] - px0) * (y[2] - py) - (x[2] - px0) * (y[1] - py);
    int64_t w1 = (x[2] - px0) * (y[0] - py) - (x[0] - px0) * (y[2] - py);
    int64_t dw0 = y[1] - y[2], dw1 = y[2] - y[0];           /* per +1 in px */
    int64_t npx = px1 - px0 + 1; int64_t i = 0;
    vq W0, W1, D0, D1, Z = {0};
    for (int k = 0; k < NQ; ++k) { W0[k] = w0 + k * dw0; W1[k] = w1 + k * dw1; D0[k] = NQ * dw0; D1[k] = NQ * dw1; }
    for (; i + NQ <= npx; i += NQ) {
        vq W2 = area - W0 - W1;
        vq in = area > 0 ? ((W0 >= Z) & (W1 >= Z) & (W2 >= Z)) : ((W0 <= Z) & (W1 <= Z) & (W2 <= Z));
        for (int k = 0; k < NQ; ++k) if (in[k]) row[px0 + i + k] = color;
        W0 += D0; W1 += D1;
    }
    for (; i < npx; ++i) {
        int64_t a0 = w0 + i * dw0, a1 = w1 + i * dw1, a2 = area - a0 - a1;
        bool in = area > 0 ? (a0 >= 0 && a1 >= 0 && a2 >= 0) : (a0 <= 0 && a1 <= 0 && a2 <= 0);
        if (in) row[px0 + i] = color;
    }
}

/* 32-bit variant for triangles whose clipped extent fits 2^14 (products stay below 2^31):
 * 2x the lanes of the int64 path and a vector select instead of a scalar scatter. The caller
 * (mxs_triangle) picks it only when the bound holds, so results equal the int64 reference. */
MXS_TGT static void KFN(k_tri_row32)(uint32_t *row, int64_t px0, int64_t px1, int64_t py, const int64_t x[3], const int64_t y[3], int64_t area64, uint32_t color) {
    int32_t area = (int32_t)area64;
    int32_t w0 = (int32_t)((x[1] - px0) * (y[2] - py) - (x[2] - px0) * (y[1] - py));
    int32_t w1 = (int32_t)((x[2] - px0) * (y[0] - py) - (x[0] - px0) * (y[2] - py));
    int32_t dw0 = (int32_t)(y[1] - y[2]), dw1 = (int32_t)(y[2] - y[0]);
    int64_t npx = px1 - px0 + 1, i = 0;
    vi W0, W1, D0, D1, Z = {0}, A; vu C;
    for (int k = 0; k < NF; ++k) { W0[k] = w0 + k * dw0; W1[k] = w1 + k * dw1; D0[k] = NF * dw0; D1[k] = NF * dw1; A[k] = area; C[k] = color; }
    uint32_t *p = row + px0;
    for (; i + NF <= npx; i += NF) {
        vi W2 = A - W0 - W1;
        vi in = area > 0 ? ((W0 >= Z) & (W1 >= Z) & (W2 >= Z)) : ((W0 <= Z) & (W1 <= Z) & (W2 <= Z));
        vu old = LD(vu, p + i);
        vu out = (C & (vu)in) | (old & ~(vu)in);
        ST(p + i, out);
        W0 += D0; W1 += D1;
    }
    for (; i < npx; ++i) {
        int32_t a0 = w0 + (int32_t)i * dw0, a1 = w1 + (int32_t)i * dw1, a2 = area - a0 - a1;
        bool in = area > 0 ? (a0 >= 0 && a1 >= 0 && a2 >= 0) : (a0 <= 0 && a1 <= 0 && a2 <= 0);
        if (in) p[i] = color;
    }
}

#undef vf
#undef vu
#undef vi
#undef vb
#undef vsb
#undef vq
#undef LD
#undef ST
#undef NF
#undef NB
#undef NQ
#undef KFN
#undef MXS_CAT
#undef MXS_CAT_
