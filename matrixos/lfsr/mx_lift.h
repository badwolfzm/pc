/*
 * mx_lift.h -- MatrixOS GF(2) affine loop lifter (v2, "nuked and rebuilt")
 *
 * A loop whose body is an affine map over GF(2)^w  (XOR, shifts, rotates,
 * AND/XOR with constants, the classic  -(x&1) & POLY  tap select, OR of
 * disjoint bit fields, multiply by a power of two) can be jumped n steps
 * ahead in O(log2 n) matrix compositions instead of n iterations.
 * Seeds, xorshift / LFSR random streams, CRC-shaped recurrences and
 * procedural-generation streams are all of this form.
 *
 * What changed against the v1 lifter in the compact prototype:
 *   1. Width-generic (8/16/32/64 bits), not hard-wired to 32.
 *   2. The recurrence is no longer hard-coded (0xEDB88320 / 0xA5A5A5A5).
 *      The pass derives the matrix from the loop body itself.
 *   3. Detection is a real analysis, not four boolean flags. A symbolic
 *      bit-slice evaluator walks a tiny straight-line IR and either proves
 *      the body is affine (and emits the exact matrix) or rejects it with
 *      a reason. Nothing is accepted on a pattern match.
 *   4. A black-box probe mode lifts an opaque step function and verifies
 *      the result with random probes (probabilistic, reported as such).
 *   5. Jumping applies precomputed 2^k powers to the *vector* (O(w) each)
 *      instead of composing them into an accumulator (O(w^2) each), and a
 *      reusable plan amortises the squarings across many seeds / jumps.
 *   6. apply() walks set bits with ctz instead of 32 branchy iterations.
 *
 * C99, no dependencies, header-only. Define MX_LIFT_IMPLEMENTATION in one
 * translation unit, or just rely on the static inline definitions.
 */
#ifndef MX_LIFT_H
#define MX_LIFT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Bit helpers                                                          */
/* ------------------------------------------------------------------ */

static inline uint64_t mx_lift_mask(unsigned w) { return w >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1u; }

static inline unsigned mx_lift_ctz64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(x);
#else
    unsigned n = 0; while (!(x & 1u)) { x >>= 1; ++n; } return n;
#endif
}

static inline bool mx_lift_width_ok(unsigned w) { return w == 8 || w == 16 || w == 32 || w == 64; }

/* ------------------------------------------------------------------ */
/* Affine map  f(x) = M x ^ add  over GF(2)^w, column form               */
/* col[i] is the image of basis vector e_i under M (bits above w are 0) */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  w;
    uint64_t add;
    uint64_t col[64];
} mx_aff;

static inline void mx_aff_identity(mx_aff *a, unsigned w) {
    memset(a, 0, sizeof(*a));
    a->w = (uint8_t)w;
    for (unsigned i = 0; i < w; ++i) a->col[i] = (uint64_t)1 << i;
}

static inline uint64_t mx_aff_apply_lin(const mx_aff *a, uint64_t x) {
    uint64_t out = 0, y = x & mx_lift_mask(a->w);
    while (y) { unsigned i = mx_lift_ctz64(y); out ^= a->col[i]; y &= y - 1; }
    return out;
}

static inline uint64_t mx_aff_apply(const mx_aff *a, uint64_t x) {
    return a->add ^ mx_aff_apply_lin(a, x);
}

/* out = a . b   (apply b first, then a) */
static inline void mx_aff_compose(mx_aff *out, const mx_aff *a, const mx_aff *b) {
    mx_aff t; t.w = a->w; t.add = mx_aff_apply(a, b->add);
    for (unsigned i = 0; i < a->w; ++i) t.col[i] = mx_aff_apply_lin(a, b->col[i]);
    for (unsigned i = a->w; i < 64; ++i) t.col[i] = 0;
    *out = t;
}

static inline bool mx_aff_equal(const mx_aff *a, const mx_aff *b) {
    if (a->w != b->w || a->add != b->add) return false;
    return memcmp(a->col, b->col, sizeof(uint64_t) * a->w) == 0;
}

/* out = a^n  (n = 0 gives identity).  O(log n) compositions. */
static inline void mx_aff_pow(mx_aff *out, const mx_aff *a, uint64_t n) {
    mx_aff acc, base = *a; mx_aff_identity(&acc, a->w);
    while (n) {
        if (n & 1u) mx_aff_compose(&acc, &base, &acc);
        n >>= 1;
        if (n) mx_aff_compose(&base, &base, &base);
    }
    *out = acc;
}

/* f^n(x) for a one-off n: log n squarings, but the running value is a
 * vector so there are no accumulator compositions. */
static inline uint64_t mx_aff_jump(const mx_aff *a, uint64_t x, uint64_t n) {
    mx_aff base = *a; x &= mx_lift_mask(a->w);
    while (n) {
        if (n & 1u) x = mx_aff_apply(&base, x);
        n >>= 1;
        if (n) mx_aff_compose(&base, &base, &base);
    }
    return x;
}

/* ------------------------------------------------------------------ */
/* Jump plan: precomputed f^(2^k), k = 0..63.  Build once per           */
/* recurrence; every later jump is <= 64 vector applies (O(w log n)).   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  w;
    uint8_t  levels;    /* number of distinct pow2 entries computed */
    mx_aff   pow2[64];
} mx_lift_plan;

static inline void mx_lift_plan_build(mx_lift_plan *p, const mx_aff *step) {
    p->w = step->w; p->pow2[0] = *step; p->levels = 64;
    for (unsigned k = 1; k < 64; ++k) mx_aff_compose(&p->pow2[k], &p->pow2[k - 1], &p->pow2[k - 1]);
}

static inline uint64_t mx_lift_plan_jump(const mx_lift_plan *p, uint64_t x, uint64_t n) {
    x &= mx_lift_mask(p->w);
    while (n) { unsigned k = mx_lift_ctz64(n); x = mx_aff_apply(&p->pow2[k], x); n &= n - 1; }
    return x;
}

/* ------------------------------------------------------------------ */
/* Loop IR: a straight-line body operating on up to MXL_REGS registers.  */
/* Register 0 holds the state on entry; `out_reg` holds it on exit.      */
/* ------------------------------------------------------------------ */

#define MXL_REGS 8

typedef enum {
    MXL_MOV = 0,     /* dst = src                                  */
    MXL_SHR,         /* dst = src >> sh          (logical)         */
    MXL_SHL,         /* dst = src << sh                            */
    MXL_ROTR,        /* dst = rotr(src, sh)                        */
    MXL_ROTL,        /* dst = rotl(src, sh)                        */
    MXL_XOR,         /* dst = src ^ src2                           */
    MXL_XOR_IMM,     /* dst = src ^ imm                            */
    MXL_AND_IMM,     /* dst = src & imm                            */
    MXL_NOT,         /* dst = ~src                                 */
    MXL_MASKSEL,     /* dst = -((src >> sh) & 1) & imm   (tap select) */
    MXL_OR,          /* dst = src | src2   (affine iff bit-disjoint) */
    MXL_AND,         /* dst = src & src2   (affine iff one side const) */
    MXL_ADD,         /* dst = src + src2   (not affine over GF(2)) */
    MXL_ADD_IMM,     /* dst = src + imm    (affine only for imm==0) */
    MXL_MUL_IMM,     /* dst = src * imm    (affine iff imm is 0 or 2^k) */
    MXL_OP_COUNT
} mxl_op;

typedef struct {
    uint8_t  op, dst, src, src2, sh;
    uint64_t imm;
} mxl_insn;

typedef struct {
    uint8_t         w;         /* 8, 16, 32 or 64 */
    uint8_t         out_reg;   /* register holding the new state after the body */
    uint16_t        n;         /* instruction count */
    const mxl_insn *code;
} mxl_loop;

static inline uint64_t mxl_rotl(uint64_t x, unsigned s, unsigned w) {
    uint64_t m = mx_lift_mask(w); s %= w; x &= m;
    return s ? ((x << s) | (x >> (w - s))) & m : x;
}
static inline uint64_t mxl_rotr(uint64_t x, unsigned s, unsigned w) { return mxl_rotl(x, (w - (s % w)) % w, w); }

/* Concrete interpreter: one loop iteration (the "naive" semantics). */
static inline uint64_t mxl_step(const mxl_loop *L, uint64_t x) {
    uint64_t r[MXL_REGS]; const uint64_t m = mx_lift_mask(L->w); unsigned w = L->w;
    memset(r, 0, sizeof(r)); r[0] = x & m;
    for (unsigned i = 0; i < L->n; ++i) {
        const mxl_insn *I = &L->code[i]; uint64_t a = r[I->src & 7], b = r[I->src2 & 7], v = 0;
        unsigned sh = I->sh;
        switch ((mxl_op)I->op) {
            case MXL_MOV:     v = a; break;
            case MXL_SHR:     v = sh >= w ? 0 : a >> sh; break;
            case MXL_SHL:     v = sh >= w ? 0 : a << sh; break;
            case MXL_ROTR:    v = mxl_rotr(a, sh, w); break;
            case MXL_ROTL:    v = mxl_rotl(a, sh, w); break;
            case MXL_XOR:     v = a ^ b; break;
            case MXL_XOR_IMM: v = a ^ I->imm; break;
            case MXL_AND_IMM: v = a & I->imm; break;
            case MXL_NOT:     v = ~a; break;
            case MXL_MASKSEL: v = (uint64_t)0 - ((sh >= w ? 0 : (a >> sh)) & 1u); v &= I->imm; break;
            case MXL_OR:      v = a | b; break;
            case MXL_AND:     v = a & b; break;
            case MXL_ADD:     v = a + b; break;
            case MXL_ADD_IMM: v = a + I->imm; break;
            case MXL_MUL_IMM: v = a * I->imm; break;
            default:          v = 0; break;
        }
        r[I->dst & 7] = v & m;
    }
    return r[L->out_reg & 7];
}

/* The naive loop: n iterations of the body. Used as the oracle in tests. */
static inline uint64_t mxl_run_naive(const mxl_loop *L, uint64_t x, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) x = mxl_step(L, x);
    return x & mx_lift_mask(L->w);
}

/* ------------------------------------------------------------------ */
/* Symbolic GF(2) bit-slice evaluation: every register bit is an affine  */
/* expression  (XOR of a set of input bits) ^ constant.                  */
/* ------------------------------------------------------------------ */

typedef struct { uint64_t lin[64]; uint64_t cst; } mxl_sym;   /* lin[j]: input bits feeding output bit j */

typedef enum {
    MXL_OK = 0,
    MXL_ERR_BAD_WIDTH,
    MXL_ERR_BAD_REG,
    MXL_ERR_BAD_OP,
    MXL_ERR_NONLINEAR_ADD,
    MXL_ERR_NONLINEAR_MUL,
    MXL_ERR_NONLINEAR_AND,
    MXL_ERR_NONLINEAR_OR,
    MXL_ERR_TOO_LONG,
    MXL_ERR_PROBE_MISMATCH
} mxl_status;

typedef struct {
    mxl_status status;
    int        insn;         /* offending instruction index, or -1 */
    uint64_t   probes;       /* probe mode: random probes run */
    uint64_t   mismatches;   /* probe mode: probes where f != M x ^ c */
} mxl_report;

static inline const char *mxl_status_str(mxl_status s) {
    switch (s) {
        case MXL_OK: return "ok";
        case MXL_ERR_BAD_WIDTH: return "width must be 8/16/32/64";
        case MXL_ERR_BAD_REG: return "register index out of range";
        case MXL_ERR_BAD_OP: return "unknown opcode";
        case MXL_ERR_NONLINEAR_ADD: return "integer add is not affine over GF(2)";
        case MXL_ERR_NONLINEAR_MUL: return "multiply by non power of two is not affine over GF(2)";
        case MXL_ERR_NONLINEAR_AND: return "and of two non-constant values is not affine";
        case MXL_ERR_NONLINEAR_OR: return "or of overlapping bit fields is not affine";
        case MXL_ERR_TOO_LONG: return "loop body too long";
        case MXL_ERR_PROBE_MISMATCH: return "step function is not affine (probe mismatch)";
        default: return "?";
    }
}

static inline bool mxl_sym_is_const(const mxl_sym *s, unsigned w) { for (unsigned j = 0; j < w; ++j) if (s->lin[j]) return false; return true; }
static inline bool mxl_sym_bit_zero(const mxl_sym *s, unsigned j) { return s->lin[j] == 0 && !((s->cst >> j) & 1u); }

/* Lift a loop body. On success *out holds the exact affine map of one iteration. */
static inline mxl_status mxl_lift(const mxl_loop *L, mx_aff *out, mxl_report *rep) {
    mxl_report r; r.status = MXL_OK; r.insn = -1; r.probes = 0; r.mismatches = 0;
    unsigned w = L->w; const uint64_t m = mx_lift_mask(w);
    if (!mx_lift_width_ok(w)) { r.status = MXL_ERR_BAD_WIDTH; goto done; }
    if (L->n > 4096) { r.status = MXL_ERR_TOO_LONG; goto done; }
    if (L->out_reg >= MXL_REGS) { r.status = MXL_ERR_BAD_REG; goto done; }

    mxl_sym regs[MXL_REGS]; /* 8 * 520 bytes on the stack; re-entrant */
    memset(regs, 0, sizeof(regs));
    for (unsigned j = 0; j < w; ++j) regs[0].lin[j] = (uint64_t)1 << j;

    for (unsigned i = 0; i < L->n; ++i) {
        const mxl_insn *I = &L->code[i]; r.insn = (int)i;
        if (I->dst >= MXL_REGS || I->src >= MXL_REGS || I->src2 >= MXL_REGS) { r.status = MXL_ERR_BAD_REG; goto done; }
        const mxl_sym *a = &regs[I->src], *b = &regs[I->src2];
        mxl_sym v; memset(&v, 0, sizeof(v));
        unsigned sh = I->sh;
        switch ((mxl_op)I->op) {
            case MXL_MOV: v = *a; break;
            case MXL_SHR:
                if (sh < w) { for (unsigned j = 0; j + sh < w; ++j) v.lin[j] = a->lin[j + sh]; v.cst = (a->cst & m) >> sh; }
                break;
            case MXL_SHL:
                if (sh < w) { for (unsigned j = sh; j < w; ++j) v.lin[j] = a->lin[j - sh]; v.cst = (a->cst << sh) & m; }
                break;
            case MXL_ROTL: case MXL_ROTR: {
                unsigned s = sh % w; if (I->op == MXL_ROTR) s = (w - s) % w;
                for (unsigned j = 0; j < w; ++j) v.lin[(j + s) % w] = a->lin[j];
                v.cst = mxl_rotl(a->cst, s, w);
                break;
            }
            case MXL_XOR: for (unsigned j = 0; j < w; ++j) v.lin[j] = a->lin[j] ^ b->lin[j]; v.cst = (a->cst ^ b->cst) & m; break;
            case MXL_XOR_IMM: v = *a; v.cst ^= I->imm & m; break;
            case MXL_AND_IMM: for (unsigned j = 0; j < w; ++j) if ((I->imm >> j) & 1u) v.lin[j] = a->lin[j]; v.cst = a->cst & I->imm & m; break;
            case MXL_NOT: v = *a; v.cst ^= m; break;
            case MXL_MASKSEL: {
                if (sh < w) for (unsigned j = 0; j < w; ++j) if ((I->imm >> j) & 1u) {
                    v.lin[j] = a->lin[sh]; if ((a->cst >> sh) & 1u) v.cst |= (uint64_t)1 << j;
                }
                break;
            }
            case MXL_OR: {
                /* a | b == a ^ b exactly when no bit position can be set in both. */
                for (unsigned j = 0; j < w; ++j) {
                    bool az = mxl_sym_bit_zero(a, j), bz = mxl_sym_bit_zero(b, j);
                    if (!az && !bz) {
                        /* both sides may be set: still affine if one side is the constant 1 (then bit is 1) */
                        bool a1 = a->lin[j] == 0 && ((a->cst >> j) & 1u), b1 = b->lin[j] == 0 && ((b->cst >> j) & 1u);
                        if (a1 || b1) { v.lin[j] = 0; v.cst |= (uint64_t)1 << j; continue; }
                        r.status = MXL_ERR_NONLINEAR_OR; goto done;
                    }
                    v.lin[j] = a->lin[j] ^ b->lin[j]; v.cst |= ((a->cst ^ b->cst) >> j & 1u) << j;
                }
                break;
            }
            case MXL_AND: {
                /* per bit: if either side is a constant, the AND is affine in the other. */
                for (unsigned j = 0; j < w; ++j) {
                    bool ac = a->lin[j] == 0, bc = b->lin[j] == 0;
                    if (ac) { if ((a->cst >> j) & 1u) { v.lin[j] = b->lin[j]; v.cst |= ((b->cst >> j) & 1u) << j; } }
                    else if (bc) { if ((b->cst >> j) & 1u) { v.lin[j] = a->lin[j]; v.cst |= ((a->cst >> j) & 1u) << j; } }
                    else { r.status = MXL_ERR_NONLINEAR_AND; goto done; }
                }
                break;
            }
            case MXL_ADD:
                /* a + b is affine only if one side is identically zero (then it is MOV). */
                if (mxl_sym_is_const(a, w) && (a->cst & m) == 0) { v = *b; break; }
                if (mxl_sym_is_const(b, w) && (b->cst & m) == 0) { v = *a; break; }
                r.status = MXL_ERR_NONLINEAR_ADD; goto done;
            case MXL_ADD_IMM:
                if ((I->imm & m) == 0) { v = *a; break; }
                r.status = MXL_ERR_NONLINEAR_ADD; goto done;
            case MXL_MUL_IMM: {
                uint64_t k = I->imm & m;
                if (k == 0) break;                                   /* v = 0 */
                if ((k & (k - 1)) == 0) {                            /* power of two: a shift */
                    unsigned s = mx_lift_ctz64(k);
                    for (unsigned j = s; j < w; ++j) v.lin[j] = a->lin[j - s];
                    v.cst = (a->cst << s) & m; break;
                }
                r.status = MXL_ERR_NONLINEAR_MUL; goto done;
            }
            default: r.status = MXL_ERR_BAD_OP; goto done;
        }
        regs[I->dst] = v;
    }
    r.insn = -1;
    {   /* transpose row form (lin[j] = inputs feeding output j) to column form */
        const mxl_sym *o = &regs[L->out_reg]; memset(out, 0, sizeof(*out)); out->w = (uint8_t)w; out->add = o->cst & m;
        for (unsigned j = 0; j < w; ++j) { uint64_t in = o->lin[j]; while (in) { unsigned i = mx_lift_ctz64(in); out->col[i] |= (uint64_t)1 << j; in &= in - 1; } }
    }
done:
    if (rep) *rep = r;
    return r.status;
}

/* ------------------------------------------------------------------ */
/* Black-box probing: lift an opaque step function.                      */
/* Exact if f is affine. If f is not affine, the random probes detect it  */
/* with probability >= 1 - 2^-probes for a map that disagrees on half of  */
/* all inputs; a map that is affine almost everywhere can slip through,   */
/* so callers that need a proof must use the IR path.                    */
/* ------------------------------------------------------------------ */

typedef uint64_t (*mx_lift_step_fn)(uint64_t x, void *ctx);

static inline uint64_t mx_lift_splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL; return z ^ (z >> 31);
}

static inline mxl_status mx_lift_probe(mx_lift_step_fn f, void *ctx, unsigned w, unsigned probes, uint64_t seed,
                                       mx_aff *out, mxl_report *rep) {
    mxl_report r; r.status = MXL_OK; r.insn = -1; r.probes = 0; r.mismatches = 0;
    const uint64_t m = mx_lift_mask(w);
    if (!mx_lift_width_ok(w)) { r.status = MXL_ERR_BAD_WIDTH; if (rep) *rep = r; return r.status; }
    memset(out, 0, sizeof(*out)); out->w = (uint8_t)w; out->add = f(0, ctx) & m;
    for (unsigned i = 0; i < w; ++i) out->col[i] = (f((uint64_t)1 << i, ctx) & m) ^ out->add;
    uint64_t s = seed ^ 0x5DEECE66DULL;
    for (unsigned k = 0; k < probes; ++k) {
        uint64_t x = mx_lift_splitmix64(&s) & m;
        if (k == 0) x = m;                                  /* always probe all-ones */
        r.probes++; if ((f(x, ctx) & m) != mx_aff_apply(out, x)) r.mismatches++;
    }
    if (r.mismatches) r.status = MXL_ERR_PROBE_MISMATCH;
    if (rep) *rep = r;
    return r.status;
}

/* ------------------------------------------------------------------ */
/* Ready-made loop bodies (also exercised by the tests)                  */
/* ------------------------------------------------------------------ */

/* Galois LFSR with affine constant: x = (x>>1) ^ (-(x&1) & poly) ^ add  -- the v1 recurrence. */
#define MXL_BODY_GALOIS_AFFINE(poly, add) \
    { {MXL_SHR,     1, 0, 0, 1, 0},   \
      {MXL_MASKSEL, 2, 0, 0, 0, (poly)}, \
      {MXL_XOR,     0, 1, 2, 0, 0},   \
      {MXL_XOR_IMM, 0, 0, 0, 0, (add)} }

/* xorshift: x ^= x<<a; x ^= x>>b; x ^= x<<c */
#define MXL_BODY_XORSHIFT(a, b, c) \
    { {MXL_SHL, 1, 0, 0, (a), 0}, {MXL_XOR, 0, 0, 1, 0, 0}, \
      {MXL_SHR, 1, 0, 0, (b), 0}, {MXL_XOR, 0, 0, 1, 0, 0}, \
      {MXL_SHL, 1, 0, 0, (c), 0}, {MXL_XOR, 0, 0, 1, 0, 0} }

#ifdef __cplusplus
}
#endif
#endif /* MX_LIFT_H */
