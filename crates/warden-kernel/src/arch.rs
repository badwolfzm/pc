//! x86-64 tables: IDT, TSS, PIC/PIT. The partition timer is the only
//! interrupt source that is unmasked; every other IRQ line stays masked.

use crate::serial::outb;
use core::mem::size_of;

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct IdtEntry {
    off_lo: u16,
    sel: u16,
    ist: u8,
    attr: u8,
    off_mid: u16,
    off_hi: u32,
    zero: u32,
}

impl IdtEntry {
    const EMPTY: IdtEntry = IdtEntry { off_lo: 0, sel: 0, ist: 0, attr: 0, off_mid: 0, off_hi: 0, zero: 0 };
    fn new(handler: u64, dpl: u8) -> Self {
        IdtEntry {
            off_lo: handler as u16,
            sel: 0x08,
            ist: 0,
            attr: 0x8E | (dpl << 5), // present, interrupt gate (IF cleared on entry)
            off_mid: (handler >> 16) as u16,
            off_hi: (handler >> 32) as u32,
            zero: 0,
        }
    }
}

#[repr(C, packed)]
struct DescPtr {
    limit: u16,
    base: u64,
}

static mut IDT: [IdtEntry; 256] = [IdtEntry::EMPTY; 256];

#[repr(C, packed)]
struct Tss {
    _r0: u32,
    rsp: [u64; 3],
    _r1: u64,
    ist: [u64; 7],
    _r2: u64,
    _r3: u16,
    iomap: u16,
}

static mut TSS: Tss = Tss { _r0: 0, rsp: [0; 3], _r1: 0, ist: [0; 7], _r2: 0, _r3: 0, iomap: size_of::<Tss>() as u16 };

extern "C" {
    static mut gdt64: [u64; 7];
    static kernel_stack_top: u8;
}


extern "C" {
    fn trap_0(); fn trap_1(); fn trap_2(); fn trap_3(); fn trap_4(); fn trap_5(); fn trap_6(); fn trap_7();
    fn trap_8(); fn trap_9(); fn trap_10(); fn trap_11(); fn trap_12(); fn trap_13(); fn trap_14(); fn trap_15();
    fn trap_16(); fn trap_17(); fn trap_18(); fn trap_19(); fn trap_20(); fn trap_21(); fn trap_22(); fn trap_23();
    fn trap_24(); fn trap_25(); fn trap_26(); fn trap_27(); fn trap_28(); fn trap_29(); fn trap_30(); fn trap_31();
    fn trap_32(); fn trap_128();
}

/// #NM: an SSE/x87/AVX instruction ran with CR0.TS set.
pub const VEC_NM: u64 = 7;
pub const VEC_TIMER: u64 = 32;
pub const VEC_SYSCALL: u64 = 128;

pub fn init() {
    let stubs: [unsafe extern "C" fn(); 32] = [
        trap_0, trap_1, trap_2, trap_3, trap_4, trap_5, trap_6, trap_7, trap_8, trap_9, trap_10, trap_11, trap_12,
        trap_13, trap_14, trap_15, trap_16, trap_17, trap_18, trap_19, trap_20, trap_21, trap_22, trap_23, trap_24,
        trap_25, trap_26, trap_27, trap_28, trap_29, trap_30, trap_31,
    ];
    unsafe {
        let idt = &mut *core::ptr::addr_of_mut!(IDT);
        for (i, s) in stubs.iter().enumerate() {
            idt[i] = IdtEntry::new(*s as *const () as usize as u64, 0);
        }
        idt[VEC_TIMER as usize] = IdtEntry::new(trap_32 as *const () as usize as u64, 0);
        // The syscall gate is the one entry ring 3 may use.
        idt[VEC_SYSCALL as usize] = IdtEntry::new(trap_128 as *const () as usize as u64, 3);
        let p = DescPtr { limit: (size_of::<[IdtEntry; 256]>() - 1) as u16, base: idt.as_ptr() as u64 };
        core::arch::asm!("lidt [{}]", in(reg) &p, options(readonly, nostack, preserves_flags));

        // TSS: ring-3 traps land on the kernel stack.
        let tss = core::ptr::addr_of_mut!(TSS);
        (*tss).rsp[0] = core::ptr::addr_of!(kernel_stack_top) as u64;
        let base = tss as u64;
        let limit = (size_of::<Tss>() - 1) as u64;
        let lo = (limit & 0xFFFF)
            | ((base & 0xFF_FFFF) << 16)
            | (0x89u64 << 40) // present, 64-bit TSS available
            | (((limit >> 16) & 0xF) << 48)
            | (((base >> 24) & 0xFF) << 56);
        let hi = base >> 32;
        let gdt = &mut *core::ptr::addr_of_mut!(gdt64);
        gdt[5] = lo;
        gdt[6] = hi;
        core::arch::asm!("ltr {0:x}", in(reg) 0x28u16, options(nomem, nostack, preserves_flags));

        // PIC: remap to 0x20/0x28, mask everything except IRQ0.
        outb(0x20, 0x11);
        outb(0xA0, 0x11);
        outb(0x21, 0x20);
        outb(0xA1, 0x28);
        outb(0x21, 0x04);
        outb(0xA1, 0x02);
        outb(0x21, 0x01);
        outb(0xA1, 0x01);
        outb(0x21, 0xFE);
        outb(0xA1, 0xFF);

        // PIT channel 0, rate generator, 100 Hz: the one partition timer.
        let div: u16 = 11932;
        outb(0x43, 0x36);
        outb(0x40, div as u8);
        outb(0x40, (div >> 8) as u8);
    }
}

extern "C" {
    fn syscall_entry();
}

unsafe fn wrmsr(msr: u32, v: u64) {
    core::arch::asm!("wrmsr", in("ecx") msr, in("eax") v as u32, in("edx") (v >> 32) as u32, options(nomem, nostack, preserves_flags));
}
unsafe fn rdmsr(msr: u32) -> u64 {
    let (lo, hi): (u32, u32);
    core::arch::asm!("rdmsr", in("ecx") msr, out("eax") lo, out("edx") hi, options(nomem, nostack, preserves_flags));
    ((hi as u64) << 32) | lo as u64
}

/// Enable the `syscall` instruction for the Linux personality: EFER.SCE,
/// STAR (kernel CS 0x08), LSTAR = entry, SFMASK clears IF, TF and DF.
/// Native threads that use it are killed by the trap handler.
pub fn init_syscall() {
    unsafe {
        wrmsr(0xC000_0081, 0x08u64 << 32);
        wrmsr(0xC000_0082, syscall_entry as *const () as usize as u64);
        wrmsr(0xC000_0084, 0x700);
        let efer = rdmsr(0xC000_0080);
        wrmsr(0xC000_0080, efer | 1);
    }
}

/// Vector state of one thread: the legacy FXSAVE image (x87, SSE) and, when
/// the CPU has XSAVE, the XSAVE header and the AVX upper halves after it.
/// 1024 bytes covers x87 | SSE | AVX in the standard format (832 bytes);
/// `init_fpu` refuses to enable any state that would not fit.
#[repr(C, align(64))]
#[derive(Clone, Copy)]
pub struct FxArea(pub [u8; 1024]);

impl FxArea {
    /// Power-on defaults: FCW 0x037F, MXCSR 0x1F80 (all exceptions masked),
    /// XSTATE_BV = x87 | SSE so XRSTOR loads exactly these values and puts
    /// the AVX upper halves in their init state (zero).
    pub const fn initial() -> Self {
        let mut a = [0u8; 1024];
        a[0] = 0x7F;
        a[1] = 0x03;
        a[24] = 0x80;
        a[25] = 0x1F;
        a[512] = 0x03;
        FxArea(a)
    }
}

/// XCR0 the kernel enabled, or 0 when the CPU has no XSAVE and the kernel
/// falls back to FXSAVE (SSE only). AVX-512 state (XCR0 bits 5..7) is never
/// enabled: the target CPU has none, and its 2 KiB of state is not worth
/// carrying for a CPU that might.
static mut XCR0: u64 = 0;

fn cpuid(leaf: u32, sub: u32) -> (u32, u32, u32, u32) {
    let (a, b, c, d): (u32, u32, u32, u32);
    // rbx is reserved by LLVM; move it through a scratch register.
    unsafe {
        core::arch::asm!("mov {t:r}, rbx", "cpuid", "xchg {t:r}, rbx", t = out(reg) b,
                         inlateout("eax") leaf => a, inlateout("ecx") sub => c, out("edx") d,
                         options(nostack, preserves_flags));
    }
    (a, b, c, d)
}

/// Turn on XSAVE and AVX for ring 3 when the CPU has them. Called once at
/// boot after CR4.OSFXSR (boot.asm). Returns the enabled XCR0 (0 = FXSAVE
/// only) and the XSAVE area size the CPU reports for it.
pub fn init_fpu() -> (u64, u32) {
    let (_, _, c1, _) = cpuid(1, 0);
    let has_xsave = c1 & (1 << 26) != 0;
    let has_avx = c1 & (1 << 28) != 0;
    if !has_xsave {
        return (0, 512);
    }
    let want: u64 = 0b011 | if has_avx { 0b100 } else { 0 };
    unsafe {
        let mut cr4: u64;
        core::arch::asm!("mov {}, cr4", out(reg) cr4, options(nomem, nostack, preserves_flags));
        cr4 |= 1 << 18; // OSXSAVE
        core::arch::asm!("mov cr4, {}", in(reg) cr4, options(nomem, nostack, preserves_flags));
        core::arch::asm!("xsetbv", in("ecx") 0u32, in("eax") want as u32, in("edx") (want >> 32) as u32,
                         options(nomem, nostack, preserves_flags));
    }
    // CPUID.(0xD,0).EBX: bytes XSAVE needs for the features now in XCR0.
    let (_, size, _, _) = cpuid(0xD, 0);
    if size as usize > core::mem::size_of::<FxArea>() {
        // Cannot happen for x87|SSE|AVX (832); fall back rather than overflow.
        unsafe { core::arch::asm!("xsetbv", in("ecx") 0u32, in("eax") 3u32, in("edx") 0u32, options(nomem, nostack, preserves_flags)) };
        unsafe { XCR0 = 3 };
        return (3, size);
    }
    unsafe { XCR0 = want };
    (want, size)
}

/// Save the live vector state into `a`: XSAVE of every enabled component,
/// or FXSAVE when the CPU has no XSAVE.
pub fn fxsave(a: &mut FxArea) {
    let m = unsafe { XCR0 };
    if m != 0 {
        unsafe { core::arch::asm!("xsave64 [{}]", in(reg) a.0.as_mut_ptr(), in("eax") m as u32, in("edx") (m >> 32) as u32, options(nostack, preserves_flags)) }
    } else {
        unsafe { core::arch::asm!("fxsave64 [{}]", in(reg) a.0.as_mut_ptr(), options(nostack, preserves_flags)) }
    }
}
/// Load `a` into the FPU. XRSTOR with the full enabled mask overwrites every
/// component (absent ones are reset to their init state), so nothing of the
/// previous owner survives in any register.
pub fn fxrstor(a: &FxArea) {
    let m = unsafe { XCR0 };
    if m != 0 {
        unsafe { core::arch::asm!("xrstor64 [{}]", in(reg) a.0.as_ptr(), in("eax") m as u32, in("edx") (m >> 32) as u32, options(nostack, preserves_flags)) }
    } else {
        unsafe { core::arch::asm!("fxrstor64 [{}]", in(reg) a.0.as_ptr(), options(nostack, preserves_flags)) }
    }
}

/// FS base for the thread about to run (Linux TLS).
pub fn set_fs_base(v: u64) {
    unsafe { wrmsr(0xC000_0100, v) }
}

/// CR0.TS: with it set, the first SSE/x87 instruction raises #NM (vector 7).
/// Native threads run with TS set, so they cannot touch the FPU at all and
/// no FPU state has to be saved or restored around them.
pub fn set_ts() {
    unsafe {
        let cr0: u64;
        core::arch::asm!("mov {}, cr0", out(reg) cr0, options(nomem, nostack, preserves_flags));
        core::arch::asm!("mov cr0, {}", in(reg) cr0 | 8, options(nomem, nostack, preserves_flags));
    }
}
pub fn clear_ts() {
    unsafe { core::arch::asm!("clts", options(nomem, nostack, preserves_flags)) }
}

pub fn timer_eoi() {
    unsafe { outb(0x20, 0x20) }
}

/// Stop QEMU with `code` (isa-debug-exit: host sees (code << 1) | 1).
pub fn qemu_exit(code: u32) -> ! {
    unsafe {
        core::arch::asm!("out dx, eax", in("dx") 0xF4u16, in("eax") code, options(nomem, nostack));
    }
    loop {
        unsafe { core::arch::asm!("cli; hlt", options(nomem, nostack)) }
    }
}
