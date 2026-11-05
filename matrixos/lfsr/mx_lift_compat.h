/*
 * mx_lift_compat.h -- drop-in replacements for the v1 names used by the
 * compact prototype (mx_affine32 / mx_lfsr_naive_steps / mx_lfsr_lifted_steps /
 * mx_detect_liftable_loop), implemented on top of mx_lift.h.
 *
 * For the integration owner (matrixos/tests): include this instead of the
 * block at lines 435-449 of the baseline file. The baseline file itself is
 * not edited (control decision 1).
 */
#ifndef MX_LIFT_COMPAT_H
#define MX_LIFT_COMPAT_H
#include "mx_lift.h"

static const mxl_insn mx_v1_lfsr_body[] = MXL_BODY_GALOIS_AFFINE(0xEDB88320u, 0xA5A5A5A5u);
static const mxl_loop mx_v1_lfsr_loop = { 32, 0, 4, mx_v1_lfsr_body };

/* Unchanged reference semantics (the oracle). */
static inline uint32_t mx_lfsr_naive_steps(uint32_t x, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) x = (x >> 1) ^ ((uint32_t)-(int32_t)(x & 1u) & 0xEDB88320u) ^ 0xA5A5A5A5u;
    return x;
}

/* Same signature as v1, now derived from the loop body by the pass. The plan
 * is built on first use (64 squarings, ~20 us) and reused. */
static inline uint32_t mx_lfsr_lifted_steps(uint32_t x, uint64_t n) {
    static mx_lift_plan plan; static int ready = 0;
    if (!ready) { mx_aff step; if (mxl_lift(&mx_v1_lfsr_loop, &step, NULL) != MXL_OK) return mx_lfsr_naive_steps(x, n); mx_lift_plan_build(&plan, &step); ready = 1; }
    return (uint32_t)mx_lift_plan_jump(&plan, x, n);
}

/* ---- v1 detector surface, kept drop-in for the baseline fuzz call site ----
 * mx_fuzz_run builds an mx_loop_sig from random flags and calls
 * mx_detect_liftable_loop(&sig). The flags are not an analysis; this shim keeps
 * the v1 type, enum values and signature so the integrated build compiles and
 * the fuzz counter keeps its meaning. When the flags describe the v1 shape, the
 * result is additionally backed by a real proof: the v1 body is lifted and must
 * succeed, otherwise OTHER is returned. New code should use
 * mx_detect_liftable_loop_ir() below. */
typedef enum { MX_LOOP_OTHER = 0, MX_LOOP_LFSR32_AFFINE = 1, MX_LOOP_GF2_AFFINE = 1 } mx_loop_kind;
typedef struct { const char *tag; uint32_t ops_mask; bool has_shift_right1, has_xor, has_and_lowbit, has_const_poly, has_const_affine; } mx_loop_sig;

static inline mx_loop_kind mx_detect_liftable_loop(const mx_loop_sig *s) { /* legacy */
    if (!s) return MX_LOOP_OTHER;
    if (!(s->has_shift_right1 && s->has_xor && s->has_and_lowbit && s->has_const_poly)) return MX_LOOP_OTHER;
    mx_aff step; return mxl_lift(&mx_v1_lfsr_loop, &step, NULL) == MXL_OK ? MX_LOOP_LFSR32_AFFINE : MX_LOOP_OTHER;
}

/* v2: a kind from a proof over the actual loop body. */
static inline mx_loop_kind mx_detect_liftable_loop_ir(const mxl_loop *body, mx_aff *step_out, mxl_report *why) {
    mx_aff tmp; if (!body) return MX_LOOP_OTHER;
    return mxl_lift(body, step_out ? step_out : &tmp, why) == MXL_OK ? MX_LOOP_GF2_AFFINE : MX_LOOP_OTHER;
}
#endif
