/* AVX register state across context switches, as a plain Linux program.
 *
 * Two copies of this program run at once, built with different FPU_ID and
 * FPU_SEED (build.rs). Each loads ymm0..ymm7 with its own pattern, then
 * yields to the other program 50 times and compares all eight registers
 * after every yield. If the kernel loses the upper halves of a ymm register
 * when it switches threads, a register comes back wrong and the check is 0.
 *
 * Built with -mgeneral-regs-only: the C code never touches a vector register,
 * so the only writers of ymm are the asm blocks below. Without AVX in the CPU
 * (CPUID.1 ECX bits 27 and 28) the program prints nothing and exits 0: the
 * check is then not verified, and the kernel's judge sees no CHECK line. */
#include "sys.h"

#ifndef FPU_ID
#define FPU_ID 164
#endif
#ifndef FPU_SEED
#define FPU_SEED 0x3a
#endif

#define NREG 8
#define ROUNDS 50
#define SYS_sched_yield 24

static u8 want[NREG * 32] __attribute__((aligned(32)));
static u8 got[NREG * 32] __attribute__((aligned(32)));

#define LD(r, off) "vmovdqu " #off "(%0), %%ymm" #r "\n\t"
#define ST(r, off) "vmovdqu %%ymm" #r ", " #off "(%0)\n\t"
#define ALL(M) M(0, 0) M(1, 32) M(2, 64) M(3, 96) M(4, 128) M(5, 160) M(6, 192) M(7, 224)

static int has_avx(void) {
    u32 a = 1, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (c & (1u << 27)) && (c & (1u << 28));
}

static void load_all(const u8 *p) { __asm__ volatile(ALL(LD) : : "r"(p) : "memory"); }
static void store_all(u8 *p) { __asm__ volatile(ALL(ST) : : "r"(p) : "memory"); }

void prog(void) {
    if (!has_avx()) {
        sys3(SYS_write, 2, "fpu: no AVX in this CPU, not verified\n", 38);
        exit_group(0);
    }
    for (int r = 0; r < NREG; r++)
        for (int i = 0; i < 32; i++)
            want[r * 32 + i] = (u8)(FPU_SEED + r * 29 + i * 7 + 1);
    load_all(want);
    int bad = 0;
    for (int round = 0; round < ROUNDS; round++) {
        sys0(SYS_sched_yield);
        store_all(got);
        for (int i = 0; i < NREG * 32; i++)
            if (got[i] != want[i]) bad++;
    }
    check(FPU_ID, bad == 0);
    exit_group(0);
}
