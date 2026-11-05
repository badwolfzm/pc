/*
 * mx_tiers.h -- MatrixOS SIMD tier contract (codegen).
 *
 * Reproducible builds forbid -march=native.  Every SIMD kernel is compiled
 * from the same source exactly three times, once per tier, and the right
 * build is selected at load time from CPUID.  Nothing about the build
 * depends on the machine that ran the compiler.
 *
 *   tier      clang flags                              x86-64 level
 *   baseline  -march=x86-64    -mtune=generic          v1  (SSE2)
 *   avx2      -march=x86-64-v3 -mtune=generic          v3  (AVX2, FMA, BMI1/2, LZCNT, MOVBE, F16C)
 *   avx512    -march=x86-64-v4 -mtune=generic          v4  (AVX-512 F/BW/CD/DQ/VL)
 *
 * Two ways to use it:
 *
 *  1. In-process dispatch (one object with three bodies).  Compile the
 *     kernel source three times with -DMX_TIER=<0|1|2> and the tier's flags
 *     (tools/mx_tier_build.sh does this).  Inside the kernel source, define
 *     the function with MX_TIER_FN(name) so each build gets its own symbol
 *     (name_baseline, name_avx2, name_avx512).  In exactly one ordinary
 *     (baseline-flag) translation unit, write
 *
 *         MX_TIER_DISPATCH(uint32_t, crc32_block, (const uint8_t *p, size_t n), (p, n))
 *
 *     which defines `crc32_block(...)` as a dispatcher resolving on first call.
 *
 *  2. Load-time selection (MXSB).  The MXSB emitter stores one payload per
 *     tier; the loader calls mx_tier_detect() and maps the highest payload
 *     whose tier is <= the detected one.  mx_tier_payload_pick() implements
 *     that rule so the emitter's tests and the loader agree.
 *
 * Safety rule: a tier is reported only when *every* feature of its level is
 * present and the OS has enabled the register state (XGETBV).  A machine with
 * AVX-512F but no AVX-512BW is an avx2 machine.  The environment variable
 * MX_TIER=baseline|avx2|avx512 can lower (never raise) the detected tier; the
 * tests use it to run every build on the one machine they have.
 *
 * C99.  No dependencies.  Header-only; functions are static inline.
 */
#ifndef MX_TIERS_H
#define MX_TIERS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MX_TIER_BASELINE = 0,
    MX_TIER_AVX2     = 1,
    MX_TIER_AVX512   = 2,
    MX_TIER__COUNT   = 3
} mx_tier;

/* The flag strings are the contract with the frontend driver: it must pass
 * exactly these (plus its own hardening/determinism flags) for each tier. */
static const char *const MX_TIER_NAME[MX_TIER__COUNT]  = { "baseline", "avx2", "avx512" };
static const char *const MX_TIER_MARCH[MX_TIER__COUNT] = { "-march=x86-64", "-march=x86-64-v3", "-march=x86-64-v4" };
static const char *const MX_TIER_MTUNE = "-mtune=generic";
/* Preprocessor define the driver adds per build; kernels read it as MX_TIER. */
static const char *const MX_TIER_DEFINE[MX_TIER__COUNT] = { "-DMX_TIER=0", "-DMX_TIER=1", "-DMX_TIER=2" };

static inline const char *mx_tier_name(mx_tier t) { return (unsigned)t < MX_TIER__COUNT ? MX_TIER_NAME[t] : "?"; }
static inline int mx_tier_parse(const char *s) {
    if (!s) return -1;
    for (int t = 0; t < MX_TIER__COUNT; ++t) if (strcmp(s, MX_TIER_NAME[t]) == 0) return t;
    return -1;
}

/* ---- CPUID probing (x86-64 only; other targets report baseline) --------- */
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#define MX_TIERS_HAVE_CPUID 1
static inline void mx_cpuid_(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(sub));
    r[0] = a; r[1] = b; r[2] = c; r[3] = d;
}
static inline uint64_t mx_xgetbv0_(void) {
    uint32_t lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t)hi << 32) | lo;
}
#else
#define MX_TIERS_HAVE_CPUID 0
#endif

typedef struct {
    bool osxsave, avx, fma, movbe, f16c, lzcnt;      /* v3 scalar/legacy-encoded bits */
    bool avx2, bmi1, bmi2;
    bool avx512f, avx512bw, avx512cd, avx512dq, avx512vl;
    bool xcr0_sse, xcr0_avx, xcr0_opmask, xcr0_zmm_hi256, xcr0_hi16_zmm;
} mx_cpu_features;

static inline void mx_cpu_features_probe(mx_cpu_features *f) {
    memset(f, 0, sizeof *f);
#if MX_TIERS_HAVE_CPUID
    uint32_t r[4];
    mx_cpuid_(0, 0, r);
    uint32_t max_leaf = r[0];
    if (max_leaf >= 1) {
        mx_cpuid_(1, 0, r);
        f->fma     = (r[2] >> 12) & 1u;
        f->movbe   = (r[2] >> 22) & 1u;
        f->osxsave = (r[2] >> 27) & 1u;
        f->avx     = (r[2] >> 28) & 1u;
        f->f16c    = (r[2] >> 29) & 1u;
    }
    if (max_leaf >= 7) {
        mx_cpuid_(7, 0, r);
        f->bmi1     = (r[1] >> 3)  & 1u;
        f->avx2     = (r[1] >> 5)  & 1u;
        f->bmi2     = (r[1] >> 8)  & 1u;
        f->avx512f  = (r[1] >> 16) & 1u;
        f->avx512dq = (r[1] >> 17) & 1u;
        f->avx512cd = (r[1] >> 28) & 1u;
        f->avx512bw = (r[1] >> 30) & 1u;
        f->avx512vl = (r[1] >> 31) & 1u;
    }
    mx_cpuid_(0x80000000u, 0, r);
    if (r[0] >= 0x80000001u) {
        mx_cpuid_(0x80000001u, 0, r);
        f->lzcnt = (r[2] >> 5) & 1u;
    }
    if (f->osxsave) {
        uint64_t x = mx_xgetbv0_();
        f->xcr0_sse        = (x >> 1) & 1u;
        f->xcr0_avx        = (x >> 2) & 1u;
        f->xcr0_opmask     = (x >> 5) & 1u;
        f->xcr0_zmm_hi256  = (x >> 6) & 1u;
        f->xcr0_hi16_zmm   = (x >> 7) & 1u;
    }
#endif
}

/* Pure function of the feature set: the tier the hardware supports. */
static inline mx_tier mx_tier_from_features(const mx_cpu_features *f) {
    bool v3 = f->osxsave && f->xcr0_sse && f->xcr0_avx &&
              f->avx && f->avx2 && f->fma && f->bmi1 && f->bmi2 && f->lzcnt && f->movbe && f->f16c;
    if (!v3) return MX_TIER_BASELINE;
    bool v4 = f->avx512f && f->avx512bw && f->avx512cd && f->avx512dq && f->avx512vl &&
              f->xcr0_opmask && f->xcr0_zmm_hi256 && f->xcr0_hi16_zmm;
    return v4 ? MX_TIER_AVX512 : MX_TIER_AVX2;
}

/* Hardware tier, lowered by MX_TIER=<name> in the environment if set.
 * `getenv_fn` lets a no-libc loader pass its own lookup (or NULL to skip). */
static inline mx_tier mx_tier_detect_with(const char *(*getenv_fn)(const char *)) {
    mx_cpu_features f; mx_cpu_features_probe(&f);
    mx_tier hw = mx_tier_from_features(&f);
    if (getenv_fn) {
        int forced = mx_tier_parse(getenv_fn("MX_TIER"));
        if (forced >= 0 && forced < (int)hw) hw = (mx_tier)forced;   /* lower only */
    }
    return hw;
}

#ifndef MX_TIERS_NO_LIBC
#include <stdlib.h>
static inline const char *mx_tiers_getenv_(const char *n) { return getenv(n); }
static inline mx_tier mx_tier_detect(void) { return mx_tier_detect_with(mx_tiers_getenv_); }
#endif

/* Load-time rule shared by the MXSB emitter and loader: given a bitmask of
 * payload tiers present in a binary (bit t set => a build for tier t exists)
 * and the detected tier, pick the highest present tier that is <= detected.
 * Returns -1 if no payload is usable (a binary with only avx512 on an avx2
 * machine). */
static inline int mx_tier_payload_pick(unsigned present_mask, mx_tier detected) {
    for (int t = (int)detected; t >= 0; --t) if (present_mask & (1u << t)) return t;
    return -1;
}

/* ---- per-build symbol naming and in-process dispatch --------------------- */
#ifndef MX_TIER
#define MX_TIER 0
#endif
#if MX_TIER == 0
#define MX_TIER_SUFFIX _baseline
#elif MX_TIER == 1
#define MX_TIER_SUFFIX _avx2
#elif MX_TIER == 2
#define MX_TIER_SUFFIX _avx512
#else
#error "MX_TIER must be 0, 1 or 2"
#endif
#define MX_TIER_CAT_(a, b) a##b
#define MX_TIER_CAT(a, b) MX_TIER_CAT_(a, b)
/* Use in the kernel source: `uint32_t MX_TIER_FN(crc32_block)(const uint8_t *p, size_t n)` */
#define MX_TIER_FN(name) MX_TIER_CAT(name, MX_TIER_SUFFIX)

/* Compile-time guard: a tier build must not be compiled with weaker flags
 * than its tier promises, or the dispatcher would run illegal instructions
 * on a machine that passed the CPUID check for a *lower* tier.  (The reverse,
 * stronger flags than the tier, is the real bug: -march=native in an avx2
 * build.  The driver's reproducibility check catches that; here we catch the
 * ones the preprocessor can see.) */
#if MX_TIER >= 1 && !defined(__AVX2__)
#error "MX_TIER=1 build compiled without AVX2 flags (-march=x86-64-v3)"
#endif
#if MX_TIER >= 2 && !(defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512DQ__))
#error "MX_TIER=2 build compiled without AVX-512 flags (-march=x86-64-v4)"
#endif
#if MX_TIER == 0 && defined(__AVX__)
#error "baseline (MX_TIER=0) build compiled with AVX flags: not a baseline build"
#endif
#if MX_TIER == 1 && defined(__AVX512F__)
#error "avx2 (MX_TIER=1) build compiled with AVX-512 flags: would fault on an avx2 machine"
#endif

/* Dispatcher: defined once, in a baseline-compiled translation unit.
 * Resolution is idempotent, so a benign race on first call is harmless. */
#define MX_TIER_DISPATCH(ret, name, params, args)                                   \
    ret name##_baseline params; ret name##_avx2 params; ret name##_avx512 params;   \
    static ret (*name##_impl_) params = 0;                                          \
    static ret name##_resolve_ params {                                             \
        ret (*fn) params = name##_baseline;                                         \
        switch (mx_tier_detect()) {                                                 \
            case MX_TIER_AVX512: fn = name##_avx512; break;                         \
            case MX_TIER_AVX2:   fn = name##_avx2;   break;                         \
            default: break;                                                         \
        }                                                                           \
        name##_impl_ = fn;                                                          \
        return fn args;                                                             \
    }                                                                               \
    ret name params { return (name##_impl_ ? name##_impl_ : name##_resolve_) args; }

#ifdef __cplusplus
}
#endif
#endif /* MX_TIERS_H */
