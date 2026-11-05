/*
 * mx_superops_compat.h - baseline-shaped dispatcher over the bounded kernels.
 *
 * Keeps the return codes of the prototype's mx_superop_dispatch so the tests
 * thread's baseline regressions still read the same, but every buffer now
 * carries an explicit capacity and the length rules of the new contract apply:
 *   -100 caller lacks MX_CAP_SUPEROP        -101 op needs MX_CAP_GPU
 *     -1 unknown op                          -2 null buffer
 *     -3 source shorter than destination     -4 length not a multiple of the element size, or zero
 *     -5 length exceeds a buffer capacity    -6 destination overlaps source
 * On any nonzero return no buffer has been written.
 */
#ifndef MX_SUPEROPS_COMPAT_H
#define MX_SUPEROPS_COMPAT_H
#include "mx_superops.h"

enum { MXC_CAP_GPU = 1u << 3, MXC_CAP_SUPEROP = 1u << 4 };
enum { MXC_OP_CRC32 = 1, MXC_OP_MEMCPY = 2, MXC_OP_GPU_VADD = 3, MXC_OP_GPU_CLEAR = 4 };

static inline int mx_superop_dispatch2(uint32_t caps, int id,
                                       void *a0, size_t a0_cap, size_t a1,
                                       void *a2, size_t a2_cap, size_t a3,
                                       uint64_t *ret) {
    if (!(caps & MXC_CAP_SUPEROP)) return -100;
    mxs_err e;
    switch (id) {
    case MXC_OP_CRC32: {
        if (!a0) return -2;
        if (a1 == 0) return -4;
        if (a1 > a0_cap) return -5;
        uint32_t c = 0;
        e = mxs_crc32(mxs_cspan_of(a0, a0_cap), a1, &c);
        if (e) return -5;
        if (ret) *ret = c;
        return 0;
    }
    case MXC_OP_MEMCPY: {
        if (!a0 || !a2) return -2;
        size_t n = a1 < a3 ? a1 : a3;             /* baseline copied min(a1,a3) */
        if (n == 0) return -4;
        if (a1 > a0_cap || a3 > a2_cap) return -5;
        e = mxs_copy(mxs_span_of(a0, a0_cap), mxs_cspan_of(a2, a2_cap), n);
        if (e == MXS_E_OVERLAP) return -6;
        if (e) return -5;
        if (ret) *ret = (uint64_t)(uintptr_t)a0;
        return 0;
    }
    case MXC_OP_GPU_VADD: {
        if (!(caps & MXC_CAP_GPU)) return -101;
        if (!a0 || !a2) return -2;
        if (a1 == 0 || (a1 & 3u) || (a3 & 3u)) return -4;
        if (a3 < a1) return -3;
        if (a1 > a0_cap || a3 > a2_cap) return -5;
        size_t n = a1 / 4u;
        /* dst[i] += src[i]: dst is both destination and first source (exact alias is legal) */
        e = mxs_vadd_f32(mxs_span_of(a0, a0_cap / 4u), mxs_cspan_of(a0, a0_cap / 4u), mxs_cspan_of(a2, a2_cap / 4u), n);
        if (e == MXS_E_OVERLAP) return -6;
        if (e == MXS_E_ALIGN) return -4;
        if (e) return -5;
        if (ret) *ret = n;
        return 0;
    }
    case MXC_OP_GPU_CLEAR: {
        if (!(caps & MXC_CAP_GPU)) return -101;
        if (!a0) return -2;
        if (a1 == 0) return -4;
        if (a1 > a0_cap) return -5;
        e = mxs_fill8(mxs_span_of(a0, a0_cap), (uint8_t)(a3 & 0xFFu), a1);
        if (e) return -5;
        if (ret) *ret = a1;
        return 0;
    }
    default:
        return -1;
    }
}
#endif
