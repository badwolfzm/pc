//! Ring-3 vector state (graphics plan M1). Native threads marked `simd` may
//! use SSE and, when the CPU has it, AVX; the kernel hands the FPU over
//! lazily on the first vector instruction (#NM) and saves the previous
//! owner's state with XSAVE (FXSAVE on CPUs without XSAVE).
//!
//! Checks:
//! - two keepers load different patterns into xmm0..15 (ymm0..15 with AVX),
//!   yield and spin across timer ticks while the other keeper runs, and get
//!   back exactly their own bytes;
//! - a snoop that only reads its vector registers sees its own init state
//!   (all zero), never a keeper's pattern;
//! - a native thread without `simd` is still killed by #NM (the existing
//!   "sse from native thread" case in tests.rs).

use core::arch::asm;

use crate::tests::DATA_PAGE;
use crate::thread::Death;

pub const ID_KEEPER_A: u64 = 300;
pub const ID_KEEPER_B: u64 = 301;
pub const ID_SNOOP: u64 = 302;

pub const EXPECT: &[(u64, &str, u64)] = &[
    (ID_KEEPER_A, "simd keeper A: vector regs survive switches", 0),
    (ID_KEEPER_B, "simd keeper B: vector regs survive switches", 0),
    (ID_SNOOP, "simd snoop: fresh thread sees zeroed regs", 0),
];

const SLOT_CONSOLE: u64 = 0;
const LOG: u64 = 0;
const YIELD: u64 = 5;
const EXIT: u64 = 6;

/// Bytes of state checked: 16 registers x 32 bytes (ymm) or x 16 (xmm).
const SAVE_IN: u64 = DATA_PAGE;
const SAVE_OUT: u64 = DATA_PAGE + 1024;

#[link_section = ".user"]
#[inline(always)]
unsafe fn sys(n: u64, a: u64, b: u64, c: u64) -> u64 {
    let r: u64;
    asm!("int 0x80", inlateout("rax") n => r, in("rdi") a, in("rsi") b, in("rdx") c, options(nostack));
    r
}

/// AVX usable from ring 3: CPUID.1:ECX OSXSAVE and AVX, then XCR0 bits 1, 2.
#[link_section = ".user"]
#[inline(always)]
unsafe fn avx_usable() -> bool {
    let c: u32;
    asm!("mov {t:r}, rbx", "cpuid", "mov rbx, {t:r}", t = out(reg) _, inlateout("eax") 1u32 => _, inlateout("ecx") 0u32 => c, out("edx") _, options(nostack));
    if c & (1 << 27) == 0 || c & (1 << 28) == 0 {
        return false;
    }
    let lo: u32;
    asm!("xgetbv", in("ecx") 0u32, out("eax") lo, out("edx") _, options(nomem, nostack));
    lo & 6 == 6
}

#[link_section = ".user"]
#[inline(always)]
unsafe fn load_regs(p: u64, avx: bool) {
    if avx {
        asm!("vmovdqu ymm0, [{p}]", "vmovdqu ymm1, [{p} + 32]", "vmovdqu ymm2, [{p} + 64]", "vmovdqu ymm3, [{p} + 96]",
             "vmovdqu ymm4, [{p} + 128]", "vmovdqu ymm5, [{p} + 160]", "vmovdqu ymm6, [{p} + 192]", "vmovdqu ymm7, [{p} + 224]",
             "vmovdqu ymm8, [{p} + 256]", "vmovdqu ymm9, [{p} + 288]", "vmovdqu ymm10, [{p} + 320]", "vmovdqu ymm11, [{p} + 352]",
             "vmovdqu ymm12, [{p} + 384]", "vmovdqu ymm13, [{p} + 416]", "vmovdqu ymm14, [{p} + 448]", "vmovdqu ymm15, [{p} + 480]",
             p = in(reg) p, options(nostack, readonly));
    } else {
        asm!("movdqu xmm0, [{p}]", "movdqu xmm1, [{p} + 16]", "movdqu xmm2, [{p} + 32]", "movdqu xmm3, [{p} + 48]",
             "movdqu xmm4, [{p} + 64]", "movdqu xmm5, [{p} + 80]", "movdqu xmm6, [{p} + 96]", "movdqu xmm7, [{p} + 112]",
             "movdqu xmm8, [{p} + 128]", "movdqu xmm9, [{p} + 144]", "movdqu xmm10, [{p} + 160]", "movdqu xmm11, [{p} + 176]",
             "movdqu xmm12, [{p} + 192]", "movdqu xmm13, [{p} + 208]", "movdqu xmm14, [{p} + 224]", "movdqu xmm15, [{p} + 240]",
             p = in(reg) p, options(nostack, readonly));
    }
}

#[link_section = ".user"]
#[inline(always)]
unsafe fn store_regs(p: u64, avx: bool) {
    if avx {
        asm!("vmovdqu [{p}], ymm0", "vmovdqu [{p} + 32], ymm1", "vmovdqu [{p} + 64], ymm2", "vmovdqu [{p} + 96], ymm3",
             "vmovdqu [{p} + 128], ymm4", "vmovdqu [{p} + 160], ymm5", "vmovdqu [{p} + 192], ymm6", "vmovdqu [{p} + 224], ymm7",
             "vmovdqu [{p} + 256], ymm8", "vmovdqu [{p} + 288], ymm9", "vmovdqu [{p} + 320], ymm10", "vmovdqu [{p} + 352], ymm11",
             "vmovdqu [{p} + 384], ymm12", "vmovdqu [{p} + 416], ymm13", "vmovdqu [{p} + 448], ymm14", "vmovdqu [{p} + 480], ymm15",
             p = in(reg) p, options(nostack));
    } else {
        asm!("movdqu [{p}], xmm0", "movdqu [{p} + 16], xmm1", "movdqu [{p} + 32], xmm2", "movdqu [{p} + 48], xmm3",
             "movdqu [{p} + 64], xmm4", "movdqu [{p} + 80], xmm5", "movdqu [{p} + 96], xmm6", "movdqu [{p} + 112], xmm7",
             "movdqu [{p} + 128], xmm8", "movdqu [{p} + 144], xmm9", "movdqu [{p} + 160], xmm10", "movdqu [{p} + 176], xmm11",
             "movdqu [{p} + 192], xmm12", "movdqu [{p} + 208], xmm13", "movdqu [{p} + 224], xmm14", "movdqu [{p} + 240], xmm15",
             p = in(reg) p, options(nostack));
    }
}

/// Load a pattern, give the CPU away several times (yield, and a spin long
/// enough to be preempted by the timer), then compare. Logs the number of
/// bytes that changed; 0 means the state survived every switch.
#[link_section = ".user"]
#[inline(always)]
unsafe fn keeper(id: u64, seed: u8) -> ! {
    let avx = avx_usable();
    let n: u64 = if avx { 512 } else { 256 };
    let inp = SAVE_IN as *mut u8;
    let mut i = 0u64;
    while i < n {
        // Different in every byte and between the two keepers.
        *inp.add(i as usize) = (i as u8).wrapping_mul(31).wrapping_add(seed) | 1;
        i += 1;
    }
    load_regs(SAVE_IN, avx);
    let mut round = 0;
    while round < 4 {
        sys(YIELD, 0, 0, 0);
        // Spin without touching vector registers; the timer preempts us.
        let mut s = 0u64;
        while s < 3_000_000 {
            asm!("/* {0} */", inout(reg) s, options(nomem, nostack));
            s += 1;
        }
        round += 1;
    }
    store_regs(SAVE_OUT, avx);
    let out = SAVE_OUT as *const u8;
    let mut bad = 0u64;
    let mut j = 0u64;
    while j < n {
        if *out.add(j as usize) != *inp.add(j as usize) {
            bad += 1;
        }
        j += 1;
    }
    sys(LOG, SLOT_CONSOLE, id, bad);
    sys(EXIT, 0, 0, 0);
    loop {}
}

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn simd_keeper_a() -> ! {
    unsafe { keeper(ID_KEEPER_A, 0x11) }
}

#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn simd_keeper_b() -> ! {
    unsafe { keeper(ID_KEEPER_B, 0xA5) }
}

/// Read the vector registers before ever writing one. A fresh thread's
/// state is the init state: every byte zero, whatever the keepers loaded.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn simd_snoop() -> ! {
    unsafe {
        // Let a keeper load its pattern first (the check holds either way).
        sys(YIELD, 0, 0, 0);
        let avx = avx_usable();
        let n: u64 = if avx { 512 } else { 256 };
        store_regs(SAVE_OUT, avx);
        let out = SAVE_OUT as *const u8;
        let mut nonzero = 0u64;
        let mut j = 0u64;
        while j < n {
            if *out.add(j as usize) != 0 {
                nonzero += 1;
            }
            j += 1;
        }
        sys(LOG, SLOT_CONSOLE, ID_SNOOP, nonzero);
        sys(EXIT, 0, 0, 0);
        loop {}
    }
}

pub struct SimdCase {
    pub name: &'static str,
    pub entry: extern "C" fn() -> !,
    pub expect: Death,
    pub budget: u32,
}

pub const CASES: &[SimdCase] = &[
    SimdCase { name: "simd keeper A", entry: simd_keeper_a, expect: Death::Exited(0), budget: 200 },
    SimdCase { name: "simd snoop", entry: simd_snoop, expect: Death::Exited(0), budget: 200 },
    SimdCase { name: "simd keeper B", entry: simd_keeper_b, expect: Death::Exited(0), budget: 200 },
];
