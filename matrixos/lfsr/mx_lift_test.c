/*
 * mx_lift_test.c -- correctness tests + benchmark for the GF(2) loop lifter.
 *
 *   cc -std=c99 -O2 -Wall -Wextra -o mx_lift_test mx_lift_test.c && ./mx_lift_test
 *
 * Every lifted result is checked against the naive loop (the IR interpreter
 * iterated n times, and for the v1 recurrence also against the original
 * hand-written C loop). Exit status 0 means every assertion passed.
 */
#define _POSIX_C_SOURCE 199309L
#include "mx_lift.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(expr) do { if (expr) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define CHECK_MSG(expr, ...) do { if (expr) g_pass++; else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s  ", __FILE__, __LINE__, #expr); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static uint64_t g_rng = 0x1234567887654321ULL;
static uint64_t rnd(void) { return mx_lift_splitmix64(&g_rng); }

/* ---- the v1 recurrence, hand written exactly as in the compact prototype ---- */
static uint32_t v1_naive_steps(uint32_t x, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) x = (x >> 1) ^ ((uint32_t)-(int32_t)(x & 1u) & 0xEDB88320u) ^ 0xA5A5A5A5u;
    return x;
}
static uint64_t v1_step_fn(uint64_t x, void *ctx) { (void)ctx; return v1_naive_steps((uint32_t)x, 1); }

/* ---- a few hand-written non-IR step functions for probe mode ---- */
static uint64_t xorshift64_fn(uint64_t x, void *c) { (void)c; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; }
static uint64_t lcg32_fn(uint64_t x, void *c) { (void)c; return (uint32_t)(x * 1664525u + 1013904223u); }
static uint64_t almost_affine_fn(uint64_t x, void *c) { (void)c; uint32_t y = (uint32_t)x; y ^= y << 5; if (y == 0xDEADBEEFu) y ^= 1u; return y; }

/* ---- loop bodies ---- */
static const mxl_insn body_v1[]        = MXL_BODY_GALOIS_AFFINE(0xEDB88320u, 0xA5A5A5A5u);
static const mxl_insn body_galois8[]   = MXL_BODY_GALOIS_AFFINE(0xB8u, 0u);
static const mxl_insn body_galois16[]  = MXL_BODY_GALOIS_AFFINE(0xB400u, 0x1234u);
static const mxl_insn body_galois64[]  = MXL_BODY_GALOIS_AFFINE(0xC96C5795D7870F42ULL, 0x0123456789ABCDEFULL);
static const mxl_insn body_xs32[]      = MXL_BODY_XORSHIFT(13, 17, 5);
static const mxl_insn body_xs64[]      = MXL_BODY_XORSHIFT(13, 7, 17);
/* 16-bit Fibonacci LFSR, taps 16,14,13,11: bit = (x ^ x>>2 ^ x>>3 ^ x>>5) & 1; x = (x>>1) | (bit<<15)   -- uses OR of disjoint fields */
static const mxl_insn body_fib16[] = {
    {MXL_SHR, 1, 0, 0, 2, 0}, {MXL_XOR, 1, 1, 0, 0, 0},
    {MXL_SHR, 2, 0, 0, 3, 0}, {MXL_XOR, 1, 1, 2, 0, 0},
    {MXL_SHR, 2, 0, 0, 5, 0}, {MXL_XOR, 1, 1, 2, 0, 0},
    {MXL_AND_IMM, 1, 1, 0, 0, 1}, {MXL_SHL, 1, 1, 0, 15, 0},
    {MXL_SHR, 0, 0, 0, 1, 0}, {MXL_OR, 0, 0, 1, 0, 0} };
/* rotate + multiply-by-8 + not: affine in a less obvious way, output in r3 */
static const mxl_insn body_rot32[] = {
    {MXL_ROTL, 1, 0, 0, 7, 0}, {MXL_MUL_IMM, 2, 0, 0, 0, 8}, {MXL_XOR, 3, 1, 2, 0, 0},
    {MXL_NOT, 3, 3, 0, 0, 0}, {MXL_ROTR, 3, 3, 0, 29, 0}, {MXL_XOR_IMM, 3, 3, 0, 0, 0xC0FFEE00u} };

typedef struct { const char *name; mxl_loop L; } named_loop;
#define NL(nm, w_, out_, body) { nm, { (w_), (out_), (uint16_t)(sizeof(body)/sizeof(body[0])), body } }
static const named_loop LOOPS[] = {
    NL("galois32+affine (v1)", 32, 0, body_v1),
    NL("galois8",              8,  0, body_galois8),
    NL("galois16+affine",     16,  0, body_galois16),
    NL("galois64+affine",     64,  0, body_galois64),
    NL("xorshift32",          32,  0, body_xs32),
    NL("xorshift64",          64,  0, body_xs64),
    NL("fibonacci16 (OR)",    16,  0, body_fib16),
    NL("rot/mul8/not32",      32,  3, body_rot32),
};
static const size_t NLOOPS = sizeof(LOOPS) / sizeof(LOOPS[0]);

static const uint64_t NS[] = { 0,1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,65,100,127,128,129,255,256,257,1000,1023,1024,1025,
                               4095,4096,4097,65535,65536,65537,1000000,1048577,16777223 };
static const size_t NNS = sizeof(NS) / sizeof(NS[0]);

/* ------------------------------------------------------------------ */
static void test_algebra(void) {
    /* identity, associativity, pow consistency on random matrices */
    const unsigned ws[4] = {8,16,32,64};
    for (int wi = 0; wi < 4; ++wi) {
        unsigned w = ws[wi]; uint64_t m = mx_lift_mask(w);
        for (int t = 0; t < 50; ++t) {
            mx_aff a, b, c, ab, bc, x1, x2, id;
            a.w = b.w = c.w = (uint8_t)w; a.add = rnd() & m; b.add = rnd() & m; c.add = rnd() & m;
            memset(a.col,0,sizeof a.col); memset(b.col,0,sizeof b.col); memset(c.col,0,sizeof c.col);
            for (unsigned i = 0; i < w; ++i) { a.col[i] = rnd() & m; b.col[i] = rnd() & m; c.col[i] = rnd() & m; }
            mx_aff_identity(&id, w);
            mx_aff_compose(&x1, &a, &id); CHECK(mx_aff_equal(&x1, &a));
            mx_aff_compose(&x1, &id, &a); CHECK(mx_aff_equal(&x1, &a));
            mx_aff_compose(&ab, &a, &b); mx_aff_compose(&x1, &ab, &c);
            mx_aff_compose(&bc, &b, &c); mx_aff_compose(&x2, &a, &bc);
            CHECK(mx_aff_equal(&x1, &x2));                                   /* (ab)c == a(bc) */
            uint64_t v = rnd() & m;
            CHECK(mx_aff_apply(&ab, v) == mx_aff_apply(&a, mx_aff_apply(&b, v)));  /* compose matches function composition */
            /* pow: a^(p+q) == a^p . a^q ; and jump equals pow-then-apply */
            uint64_t p = rnd() % 5000, q = rnd() % 5000; mx_aff ap, aq, apq, prod;
            mx_aff_pow(&ap, &a, p); mx_aff_pow(&aq, &a, q); mx_aff_pow(&apq, &a, p + q); mx_aff_compose(&prod, &ap, &aq);
            CHECK(mx_aff_equal(&apq, &prod));
            CHECK(mx_aff_jump(&a, v, p) == mx_aff_apply(&ap, v));
            /* p=0 is identity */
            mx_aff_pow(&ap, &a, 0); CHECK(mx_aff_equal(&ap, &id)); CHECK(mx_aff_jump(&a, v, 0) == v);
        }
    }
}

/* ------------------------------------------------------------------ */
static void test_lift_vs_naive(void) {
    for (size_t li = 0; li < NLOOPS; ++li) {
        const mxl_loop *L = &LOOPS[li].L; const uint64_t m = mx_lift_mask(L->w);
        mx_aff A, A2; mxl_report rep;
        mxl_status st = mxl_lift(L, &A, &rep);
        CHECK_MSG(st == MXL_OK, "%s: %s (insn %d)", LOOPS[li].name, mxl_status_str(st), rep.insn);
        if (st != MXL_OK) continue;
        mxl_lift(L, &A2, NULL); CHECK(mx_aff_equal(&A, &A2));              /* deterministic */
        /* one step of the matrix == one step of the interpreter, for many x including edge values */
        for (int t = 0; t < 2000; ++t) {
            uint64_t x = t == 0 ? 0 : t == 1 ? m : t == 2 ? 1 : t == 3 ? ((uint64_t)1 << (L->w - 1)) : (rnd() & m);
            CHECK_MSG(mx_aff_apply(&A, x) == mxl_step(L, x), "%s one-step x=%" PRIx64, LOOPS[li].name, x);
        }
        mx_lift_plan P; mx_lift_plan_build(&P, &A);
        /* lifted n steps == naive n steps, for a grid of n and random seeds (3 ways of jumping) */
        for (size_t ni = 0; ni < NNS; ++ni) {
            uint64_t n = NS[ni];
            int seeds = n > 100000 ? 2 : 6;
            for (int s = 0; s < seeds; ++s) {
                uint64_t x = s == 0 ? 0 : (rnd() & m);
                uint64_t ref = mxl_run_naive(L, x, n);
                uint64_t j1 = mx_aff_jump(&A, x, n);
                uint64_t j2 = mx_lift_plan_jump(&P, x, n);
                mx_aff An; mx_aff_pow(&An, &A, n); uint64_t j3 = mx_aff_apply(&An, x);
                CHECK_MSG(j1 == ref, "%s jump n=%" PRIu64 " x=%" PRIx64 " got %" PRIx64 " want %" PRIx64, LOOPS[li].name, n, x, j1, ref);
                CHECK_MSG(j2 == ref, "%s plan n=%" PRIu64, LOOPS[li].name, n);
                CHECK_MSG(j3 == ref, "%s pow n=%" PRIu64, LOOPS[li].name, n);
            }
        }
        /* far jumps (no naive oracle possible): f^(a+b) = f^a . f^b and jump(jump) consistency, 64-bit n */
        for (int t = 0; t < 200; ++t) {
            uint64_t a = rnd(), b = rnd(), x = rnd() & m, sum = a + b;         /* wraps mod 2^64; the plan is defined mod 2^64 too */
            uint64_t ja = mx_lift_plan_jump(&P, x, a), jab = mx_lift_plan_jump(&P, ja, b);
            if (sum >= a) CHECK(mx_lift_plan_jump(&P, x, sum) == jab);        /* only when a+b did not overflow */
            CHECK(mx_aff_jump(&A, x, a) == ja);
        }
    }
    /* the v1 hand-written C loop vs the matrix lifted from its IR */
    {
        mx_aff A; CHECK(mxl_lift(&LOOPS[0].L, &A, NULL) == MXL_OK);
        mx_lift_plan P; mx_lift_plan_build(&P, &A);
        for (size_t ni = 0; ni < NNS; ++ni) for (int s = 0; s < 4; ++s) {
            uint32_t x = (uint32_t)rnd(); uint64_t n = NS[ni];
            CHECK(v1_naive_steps(x, n) == (uint32_t)mx_lift_plan_jump(&P, x, n));
        }
        /* and the exact case the compact prototype asserted */
        CHECK(v1_naive_steps(0x12345678u, 1000000u) == (uint32_t)mx_aff_jump(&A, 0x12345678u, 1000000u));
    }
}

/* ------------------------------------------------------------------ */
static void test_probe_mode(void) {
    mx_aff A, B; mxl_report rep;
    /* probing the opaque v1 function recovers exactly the matrix the IR pass proves */
    CHECK(mx_lift_probe(v1_step_fn, NULL, 32, 128, 1, &A, &rep) == MXL_OK); CHECK(rep.mismatches == 0);
    CHECK(mxl_lift(&LOOPS[0].L, &B, NULL) == MXL_OK); CHECK(mx_aff_equal(&A, &B));
    /* xorshift64 opaque vs IR */
    CHECK(mx_lift_probe(xorshift64_fn, NULL, 64, 128, 2, &A, &rep) == MXL_OK);
    CHECK(mxl_lift(&LOOPS[5].L, &B, NULL) == MXL_OK); CHECK(mx_aff_equal(&A, &B));
    for (int t = 0; t < 100; ++t) { uint64_t x = rnd(), n = rnd() % 3000, y = x; for (uint64_t i = 0; i < n; ++i) y = xorshift64_fn(y, NULL); CHECK(mx_aff_jump(&A, x, n) == y); }
    /* an LCG is not affine over GF(2): must be rejected */
    CHECK(mx_lift_probe(lcg32_fn, NULL, 32, 64, 3, &A, &rep) == MXL_ERR_PROBE_MISMATCH); CHECK(rep.mismatches > 0);
    /* a function that is affine except at one input: probes almost surely miss it. This documents the limit of probe mode. */
    mxl_status st = mx_lift_probe(almost_affine_fn, NULL, 32, 256, 4, &A, &rep);
    printf("  probe-mode limit: almost-affine function -> %s (expected to be accepted; the IR path is the proof path)\n", mxl_status_str(st));
    CHECK(mx_lift_probe(v1_step_fn, NULL, 12, 8, 0, &A, &rep) == MXL_ERR_BAD_WIDTH);
}

/* ------------------------------------------------------------------ */
static void test_rejections(void) {
    mx_aff A; mxl_report rep;
    { mxl_insn c[] = { {MXL_SHL,1,0,0,3,0}, {MXL_ADD,0,0,1,0,0} }; mxl_loop L = {32,0,2,c};
      CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_NONLINEAR_ADD); CHECK(rep.insn == 1); }
    { mxl_insn c[] = { {MXL_MUL_IMM,0,0,0,0,1664525u}, {MXL_ADD_IMM,0,0,0,0,1013904223u} }; mxl_loop L = {32,0,2,c};
      CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_NONLINEAR_MUL); CHECK(rep.insn == 0); }
    { mxl_insn c[] = { {MXL_ADD_IMM,0,0,0,0,1} }; mxl_loop L = {32,0,1,c}; CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_NONLINEAR_ADD); }
    { mxl_insn c[] = { {MXL_SHR,1,0,0,1,0}, {MXL_AND,0,0,1,0,0} }; mxl_loop L = {32,0,2,c}; CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_NONLINEAR_AND); }
    { mxl_insn c[] = { {MXL_SHR,1,0,0,1,0}, {MXL_OR,0,0,1,0,0} }; mxl_loop L = {32,0,2,c}; CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_NONLINEAR_OR); }
    { mxl_insn c[] = { {MXL_MOV,0,0,0,0,0} }; mxl_loop L = {12,0,1,c}; CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_BAD_WIDTH); }
    { mxl_insn c[] = { {MXL_MOV,9,0,0,0,0} }; mxl_loop L = {32,0,1,c}; CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_BAD_REG); }
    { mxl_insn c[] = { {MXL_OP_COUNT,0,0,0,0,0} }; mxl_loop L = {32,0,1,c}; CHECK(mxl_lift(&L,&A,&rep) == MXL_ERR_BAD_OP); }
    /* accepted "nonlinear-looking" forms that are affine: AND with a constant register, OR with constant 1 bits, add of zero, mul by 2^k */
    { mxl_insn c[] = { {MXL_AND_IMM,1,0,0,0,0}, {MXL_XOR_IMM,1,1,0,0,0x0F0Fu}, {MXL_AND,0,0,1,0,0} }; mxl_loop L = {16,0,3,c};
      CHECK(mxl_lift(&L,&A,&rep) == MXL_OK); for (int t=0;t<200;++t){ uint64_t x=rnd()&0xFFFF; CHECK(mx_aff_apply(&A,x)==mxl_step(&L,x)); } }
    { mxl_insn c[] = { {MXL_AND_IMM,1,0,0,0,0}, {MXL_XOR_IMM,1,1,0,0,0xFF00u}, {MXL_OR,0,0,1,0,0} }; mxl_loop L = {16,0,3,c};
      CHECK(mxl_lift(&L,&A,&rep) == MXL_OK); for (int t=0;t<200;++t){ uint64_t x=rnd()&0xFFFF; CHECK(mx_aff_apply(&A,x)==mxl_step(&L,x)); } }
    { mxl_insn c[] = { {MXL_AND_IMM,1,0,0,0,0}, {MXL_ADD,0,0,1,0,0}, {MXL_MUL_IMM,0,0,0,0,16} }; mxl_loop L = {32,0,3,c};
      CHECK(mxl_lift(&L,&A,&rep) == MXL_OK); for (int t=0;t<200;++t){ uint64_t x=rnd()&0xFFFFFFFFu; CHECK(mx_aff_apply(&A,x)==mxl_step(&L,x)); } }
}

/* ------------------------------------------------------------------ */
/* Fuzz: random IR bodies. Linear-only bodies must lift and match the      */
/* naive loop. Bodies with nonlinear ops must be rejected or, if accepted  */
/* (because the op happened to be affine in context), still match.         */
/* Soundness invariant: accepted => matches naive, always.                 */
static void fuzz(unsigned iters, bool allow_nonlinear, unsigned *accepted, unsigned *rejected) {
    const unsigned ws[4] = {8,16,32,64};
    *accepted = *rejected = 0;
    for (unsigned it = 0; it < iters; ++it) {
        unsigned w = ws[rnd() % 4]; uint64_t m = mx_lift_mask(w);
        mxl_insn code[14]; unsigned n = 1 + rnd() % 13;
        for (unsigned i = 0; i < n; ++i) {
            mxl_insn I; unsigned lin_ops = MXL_OR + 1; /* MOV..OR, AND excluded from the linear set */
            unsigned op = allow_nonlinear ? (rnd() % MXL_OP_COUNT) : (rnd() % lin_ops);
            if (!allow_nonlinear && op == MXL_OR) op = MXL_XOR; /* OR on random operands is usually rejected; keep the pure-linear fuzz pure */
            I.op = (uint8_t)op; I.dst = rnd() % 4; I.src = rnd() % 4; I.src2 = rnd() % 4; I.sh = rnd() % (w + 2);
            I.imm = rnd() & m; if (op == MXL_MUL_IMM && (rnd() & 1)) I.imm = (uint64_t)1 << (rnd() % w);
            code[i] = I;
        }
        mxl_loop L; L.w = (uint8_t)w; L.out_reg = rnd() % 4; L.n = (uint16_t)n; L.code = code;
        mx_aff A; mxl_report rep; mxl_status st = mxl_lift(&L, &A, &rep);
        if (!allow_nonlinear) CHECK_MSG(st == MXL_OK, "linear fuzz body rejected: %s insn %d", mxl_status_str(st), rep.insn);
        if (st != MXL_OK) { (*rejected)++; continue; }
        (*accepted)++;
        mx_lift_plan P; mx_lift_plan_build(&P, &A);
        for (int k = 0; k < 6; ++k) {
            uint64_t x = rnd() & m, nn = k == 0 ? 1 : rnd() % 3000;
            uint64_t ref = mxl_run_naive(&L, x, nn);
            CHECK_MSG(mx_lift_plan_jump(&P, x, nn) == ref, "fuzz it=%u w=%u n=%" PRIu64, it, w, nn);
            CHECK_MSG(mx_aff_jump(&A, x, nn) == ref, "fuzz(jump) it=%u", it);
        }
    }
}

/* ------------------------------------------------------------------ */
static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b; return x < y ? -1 : x > y; }

static void bench(void) {
    /* Harness rules from the baseline: warmup, repeated runs, median and p95, timer floor, outputs consumed. */
    enum { RUNS = 21, WARM = 3 }; uint64_t s[RUNS], consumed = 0;
    uint64_t floor_ns = ~0ull; for (int i = 0; i < 1000; ++i) { uint64_t a = now_ns(), b = now_ns(); if (b - a < floor_ns) floor_ns = b - a; }
    const uint64_t N = 10000000ull; uint32_t seed = 0xCAFEBABEu;
    mx_aff A; mxl_lift(&LOOPS[0].L, &A, NULL); mx_lift_plan P; mx_lift_plan_build(&P, &A);
    printf("  timer floor %" PRIu64 " ns, N=%" PRIu64 ", %d runs after %d warmup, median / p95\n", floor_ns, N, RUNS, WARM);
    struct { const char *name; int kind; } cases[] = { {"naive loop (v1 C)",0}, {"v1 lifted (accumulator compose)",1}, {"v2 mx_aff_jump (one-off n)",2}, {"v2 plan build + one jump (cold)",4}, {"v2 plan jump only (warm)",3} };
    for (int c = 0; c < 5; ++c) {
        for (int r = -WARM; r < RUNS; ++r) {
            uint64_t t0 = now_ns(); uint32_t out = 0;
            switch (cases[c].kind) {
                case 0: out = v1_naive_steps(seed + (uint32_t)r, N); break;
                case 1: { mx_aff acc, base = A; mx_aff_identity(&acc, 32); uint64_t n = N;
                          while (n) { if (n & 1) mx_aff_compose(&acc, &base, &acc); n >>= 1; if (n) mx_aff_compose(&base, &base, &base); }
                          out = (uint32_t)mx_aff_apply(&acc, seed + (uint32_t)r); break; }
                case 2: out = (uint32_t)mx_aff_jump(&A, seed + (uint32_t)r, N); break;
                case 3: out = (uint32_t)mx_lift_plan_jump(&P, seed + (uint32_t)r, N); break;
                case 4: { mx_lift_plan Pc; mx_lift_plan_build(&Pc, &A); out = (uint32_t)mx_lift_plan_jump(&Pc, seed + (uint32_t)r, N); break; }
            }
            uint64_t t1 = now_ns(); consumed ^= out + (uint64_t)c; if (r >= 0) s[r] = t1 - t0;
        }
        qsort(s, RUNS, sizeof(s[0]), cmp_u64);
        printf("  %-36s median %12" PRIu64 " ns   p95 %12" PRIu64 " ns%s\n", cases[c].name, s[RUNS/2], s[(RUNS*95)/100], s[RUNS/2] < 10*floor_ns ? "  (near timer floor)" : "");
    }
    /* plan build cost (paid once per recurrence) */
    { uint64_t t0 = now_ns(); for (int i = 0; i < 100; ++i) { mx_lift_plan_build(&P, &A); consumed ^= P.pow2[63].add; } uint64_t t1 = now_ns();
      printf("  plan build (64 squarings, 32-bit)    %12" PRIu64 " ns each\n", (t1 - t0) / 100); }
    printf("  consumed-output hash 0x%016" PRIx64 "\n", consumed);
}

int main(int argc, char **argv) {
    bool do_bench = argc < 2 || strcmp(argv[1], "--no-bench") != 0;
    printf("mx_lift v2 tests\n");
    test_algebra();            printf("  algebra: %d checks so far, %d failed\n", g_pass, g_fail);
    test_lift_vs_naive();      printf("  lift vs naive: %d checks so far, %d failed\n", g_pass, g_fail);
    test_probe_mode();         printf("  probe mode: %d checks so far, %d failed\n", g_pass, g_fail);
    test_rejections();         printf("  rejections: %d checks so far, %d failed\n", g_pass, g_fail);
    unsigned acc, rej;
    fuzz(20000, false, &acc, &rej); printf("  fuzz linear-only: %u lifted, %u rejected; %d checks so far, %d failed\n", acc, rej, g_pass, g_fail);
    fuzz(20000, true, &acc, &rej);  printf("  fuzz with nonlinear ops: %u lifted (all verified against naive), %u rejected; %d checks so far, %d failed\n", acc, rej, g_pass, g_fail);
    if (do_bench) { printf("bench\n"); bench(); }
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
