//! Pong inside Warden: a display device, a keyboard, five syscalls and the
//! boot setup that runs the C guest app `guest/pong/` next to three hostile
//! ring-3 threads. Built only with `--features pong`; the escape suite is
//! untouched without it.
//!
//! Display: the Bochs/QEMU VGA adapter (PCI 1234:1111) in its linear
//! framebuffer mode, 320x240x32, found by a PCI config-space scan and
//! programmed through the dispi I/O ports. The framebuffer is mapped
//! supervisor-only into every address space, so no thread can touch pixels
//! except through `SYS_PRESENT`, which needs a capability to the one display
//! object (kind `Frame`, WRITE). Keyboard: the i8042 output buffer, polled
//! inside `SYS_KEY` (READ on the display). No new interrupt is unmasked.
//!
//! Syscalls 60..64 (reserved range "free, ask the lead"; requested in
//! CONTROL.md):
//!   60 DISPLAY_INFO (slot)                -> w | h << 16
//!   61 PRESENT      (slot, px, x|y<<16|w<<32|h<<48) -> 0: copy that rect of a
//!                   display-stride ARGB buffer in the caller's memory to the screen
//!   62 KEY          (slot)                -> next scancode or 0
//!   63 TICKS        (slot)                -> partition timer ticks
//!   64 REPORT       (console_slot, key, value) -> 0; printed as `report <thread> <key> <value>`
#![cfg(any(feature = "pong", feature = "game_probe"))]
#![allow(clippy::empty_loop)]

use crate::mm::{self, AddressSpace, FrameAlloc, Perm};
use crate::serial::inb;
use crate::thread::{self, Death, Kernel, TrapFrame};
use crate::{arch, heap, kprintln};
use core::arch::asm;
use warden_caps::{CapError, ObjectId, ObjectKind, Rights};

pub const SYS_DISPLAY_INFO: u64 = 60;
pub const SYS_PRESENT: u64 = 61;
pub const SYS_KEY: u64 = 62;
pub const SYS_TICKS: u64 = 63;
pub const SYS_REPORT: u64 = 64;

const FB_W: u32 = 320;
const FB_H: u32 = 240;
const CODE_BASE: u64 = 0x80_0000;
const DATA_BASE: u64 = 0x84_0000;
const HEAP_END: u64 = 0x9E_0000;
const STACK_LOW: u64 = 0x9F_0000;
/// The guest layout is pinned to 0x800000..0xA00000 (where the kernel's own
/// `.user` partition sits) whatever the user window's current bounds are.
const STACK_TOP: u64 = 0xA0_0000;
const HOSTILE_CODE: u64 = 0x80_0000;
const MAX_THREADS: usize = 8;

#[cfg(feature = "pong")]
static PONG_TEXT: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/pong_text.bin"));
#[cfg(feature = "pong")]
static PONG_DATA: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/pong_data.bin"));
#[cfg(feature = "game_probe")]
static PONG_TEXT: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/game-probe_text.bin"));
#[cfg(feature = "game_probe")]
static PONG_DATA: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/game-probe_data.bin"));

/// A fallback framebuffer in kernel memory when no VGA device is found, so the
/// run (and its checks) work on a machine without a display.
static mut FAKE_LFB: [u32; (FB_W * FB_H) as usize] = [0; (FB_W * FB_H) as usize];

struct Display {
    lfb: u64,
    pitch: u64,
    real: bool,
    object: Option<ObjectId>,
}
static mut DISPLAY: Display = Display { lfb: 0, pitch: (FB_W * 4) as u64, real: false, object: None };

/// Per-thread accounting: (presents accepted, presents refused, tiles bytes).
static mut PRESENTS: [[u64; 3]; MAX_THREADS] = [[0; 3]; MAX_THREADS];
/// Reports by key, last value wins (every thread; keys are disjoint by convention).
const NREPORT_MAX: usize = 64;
static mut REPORTS: [(u64, u64); NREPORT_MAX] = [(0, 0); NREPORT_MAX];
static mut NREPORTS: usize = 0;
static mut PONG_THREAD: usize = usize::MAX;
static mut KEYS_SEEN: u64 = 0;

fn d() -> &'static mut Display {
    unsafe { &mut *core::ptr::addr_of_mut!(DISPLAY) }
}

// ---- hardware -----------------------------------------------------------------------------------

unsafe fn outl(port: u16, v: u32) {
    asm!("out dx, eax", in("dx") port, in("eax") v, options(nomem, nostack, preserves_flags));
}
unsafe fn inl(port: u16) -> u32 {
    let v: u32;
    asm!("in eax, dx", in("dx") port, out("eax") v, options(nomem, nostack, preserves_flags));
    v
}
unsafe fn outw(port: u16, v: u16) {
    asm!("out dx, ax", in("dx") port, in("ax") v, options(nomem, nostack, preserves_flags));
}

fn pci_read(bus: u32, dev: u32, func: u32, reg: u32) -> u32 {
    let addr = 0x8000_0000 | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xFC);
    unsafe {
        outl(0xCF8, addr);
        inl(0xCFC)
    }
}

/// Find the Bochs VGA adapter's framebuffer BAR, or None.
fn find_vga_lfb() -> Option<u64> {
    for dev in 0..32 {
        let id = pci_read(0, dev, 0, 0);
        if id == 0xFFFF_FFFF {
            continue;
        }
        let (vendor, device) = (id & 0xFFFF, id >> 16);
        let class = pci_read(0, dev, 0, 8) >> 16; // class 0x03 = display
        if (vendor == 0x1234 && device == 0x1111) || class == 0x0300 {
            let bar0 = pci_read(0, dev, 0, 0x10);
            if bar0 & 1 == 0 && bar0 & !0xF != 0 {
                return Some((bar0 & !0xF) as u64);
            }
        }
    }
    None
}

/// Program the dispi (Bochs VBE) registers: 320x240x32, linear framebuffer.
fn bga_set_mode() {
    const INDEX: u16 = 0x1CE;
    const DATA: u16 = 0x1CF;
    unsafe {
        outw(INDEX, 4); outw(DATA, 0); // disable
        outw(INDEX, 1); outw(DATA, FB_W as u16); // xres
        outw(INDEX, 2); outw(DATA, FB_H as u16); // yres
        outw(INDEX, 3); outw(DATA, 32); // bpp
        outw(INDEX, 6); outw(DATA, FB_W as u16); // virtual width = pitch in pixels
        outw(INDEX, 4); outw(DATA, 0x41); // enabled | LFB
    }
}

/// Map `[base, base + 16 MiB)` with 2 MiB supervisor pages into the page
/// directory tree rooted at `pml4` (kernel-only: the user bit is clear on the
/// PDPT entry). The directory frame is shared by every address space.
fn map_lfb_into(pml4: u64, pd: u64, base: u64) {
    unsafe {
        let pdpt = (*(pml4 as *const u64)) & 0x000F_FFFF_FFFF_F000;
        let pdpt_idx = ((base >> 30) & 511) as usize;
        *(pdpt as *mut u64).add(pdpt_idx) = pd | 0x3; // present | writable, supervisor
    }
}
fn fill_lfb_pd(pd: u64, base: u64) {
    let start = base & !((2 << 20) - 1);
    for i in 0..8u64 {
        let phys = start + i * (2 << 20);
        let idx = ((phys >> 21) & 511) as usize;
        // present | writable | huge | cache-disable (framebuffer memory)
        unsafe { *(pd as *mut u64).add(idx) = phys | 0x83 | 0x10 };
    }
}

extern "C" {
    static boot_pml4: [u64; 512];
    static __user_start: u8;
    static __user_end: u8;
}

// ---- syscalls -----------------------------------------------------------------------------------

fn err(e: CapError) -> u64 {
    -(match e {
        CapError::InvalidSlot => 1i64,
        CapError::SlotOccupied => 2,
        CapError::InsufficientRights => 3,
        CapError::Revoked => 4,
        CapError::OutOfBudget => 5,
        CapError::WrongKind => 6,
    }) as u64
}

/// The capability in `slot` must name the display object with `need`.
fn display_cap(k: &Kernel, slot: u64, need: Rights) -> Result<(), u64> {
    let cur = k.current;
    let r = k.caps.invoke(&k.threads[cur].cspace, slot as usize, need).map_err(err)?;
    if r.kind != ObjectKind::Frame || Some(r.object) != d().object {
        return Err(err(CapError::WrongKind));
    }
    Ok(())
}

/// Copy a rect out of the caller's buffer into the framebuffer. Every address
/// is checked against the caller's own mappings before a byte moves; the
/// kernel reads the user pages through the caller's page tables, which are
/// the current ones.
fn present(k: &mut Kernel, px: u64, rect: u64) -> Result<u64, u64> {
    let cur = k.current;
    let (x, y, w, h) = (rect & 0xFFFF, (rect >> 16) & 0xFFFF, (rect >> 32) & 0xFFFF, (rect >> 48) & 0xFFFF);
    const E_ARG: u64 = -(10i64) as u64;
    if w == 0 || h == 0 || x + w > FB_W as u64 || y + h > FB_H as u64 {
        return Err(E_ARG);
    }
    if px < mm::USER_BASE || px % 4 != 0 || px + (FB_W * FB_H * 4) as u64 > mm::USER_END {
        return Err(E_ARG);
    }
    let a = &k.threads[cur].aspace;
    let stride = (FB_W * 4) as u64;
    for row in 0..h {
        let src = px + (y + row) * stride + x * 4;
        let end = src + w * 4 - 1;
        if !a.is_mapped(src & !(mm::PAGE - 1)) || !a.is_mapped(end & !(mm::PAGE - 1)) {
            return Err(E_ARG);
        }
    }
    let disp = d();
    for row in 0..h {
        let src = (px + (y + row) * stride + x * 4) as *const u32;
        let dst = (disp.lfb + (y + row) * disp.pitch + x * 4) as *mut u32;
        unsafe { core::ptr::copy_nonoverlapping(src, dst, w as usize) };
    }
    unsafe {
        PRESENTS[cur.min(MAX_THREADS - 1)][0] += 1;
        PRESENTS[cur.min(MAX_THREADS - 1)][2] += w * h * 4;
    }
    Ok(0)
}

fn key_poll() -> u64 {
    unsafe {
        if inb(0x64) & 1 != 0 {
            inb(0x60) as u64
        } else {
            0
        }
    }
}

/// Syscall numbers 60..=64. Returns the value for rax or a negative error.
pub fn syscall(k: &mut Kernel, f: &TrapFrame) -> Result<u64, u64> {
    let cur = k.current;
    let (a, b, c) = (f.rdi, f.rsi, f.rdx);
    match f.rax {
        SYS_DISPLAY_INFO => {
            display_cap(k, a, Rights::READ)?;
            Ok(FB_W as u64 | ((FB_H as u64) << 16))
        }
        SYS_PRESENT => {
            if let Err(e) = display_cap(k, a, Rights::WRITE) {
                unsafe { PRESENTS[cur.min(MAX_THREADS - 1)][1] += 1 };
                return Err(e);
            }
            present(k, b, c).map_err(|e| {
                unsafe { PRESENTS[cur.min(MAX_THREADS - 1)][1] += 1 };
                e
            })
        }
        SYS_KEY => {
            display_cap(k, a, Rights::READ)?;
            let sc = key_poll();
            if sc != 0 {
                unsafe { KEYS_SEEN += 1 };
            }
            Ok(sc)
        }
        SYS_TICKS => {
            display_cap(k, a, Rights::READ)?;
            Ok(k.ticks)
        }
        SYS_REPORT => {
            // Reporting needs the console capability, like SYS_LOG.
            k.caps.invoke(&k.threads[cur].cspace, a as usize, Rights::WRITE).map_err(err)?;
            let name = k.threads[cur].name;
            kprintln!("report {name} {b} {c}");
            {
                // Keys are unique per thread by convention (pong 1..99, hostiles 200+).
                unsafe {
                    let n = NREPORTS;
                    if let Some(slot) = REPORTS[..n].iter_mut().find(|(key, _)| *key == b) {
                        slot.1 = c;
                    } else if n < NREPORT_MAX {
                        REPORTS[n] = (b, c);
                        NREPORTS = n + 1;
                    }
                }
            }
            Ok(0)
        }
        _ => Err(-(100i64) as u64),
    }
}

fn report(key: u64) -> Option<u64> {
    unsafe { REPORTS[..NREPORTS].iter().find(|(k, _)| *k == key).map(|(_, v)| *v) }
}

// ---- hostile ring-3 threads (same conventions as tests.rs) ---------------------------------------

macro_rules! sys {
    ($n:expr, $a:expr, $b:expr, $c:expr) => {{
        let r: u64;
        asm!("int 0x80", inlateout("rax") $n as u64 => r, in("rdi") $a as u64, in("rsi") $b as u64, in("rdx") $c as u64,
             options(nostack));
        r
    }};
}

const E_INVALID: u64 = -(1i64) as u64;
const E_RIGHTS: u64 = -(3i64) as u64;
const E_KIND: u64 = -(6i64) as u64;

/// Tries to draw without the display capability, floods the kernel with
/// refused presents, then reads and writes Pong's memory addresses. Expected
/// end: #PF on the first touch, because its own address space has nothing
/// mapped there.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_writer() -> ! {
    unsafe {
        let rect = 0u64 | (0u64 << 16) | (16u64 << 32) | (16u64 << 48);
        let r = sys!(SYS_PRESENT, 1u64, DATA_BASE, rect); // slot 1 is empty for this thread
        sys!(SYS_REPORT, 0u64, 200u64, r);
        let r = sys!(SYS_PRESENT, 0u64, DATA_BASE, rect); // console cap: wrong object
        sys!(SYS_REPORT, 0u64, 201u64, r);
        let r = sys!(SYS_KEY, 0u64, 0u64, 0u64);
        sys!(SYS_REPORT, 0u64, 202u64, r);
        let mut bad = 0u64;
        let mut i = 0u64;
        while i < 20000 {
            let r = sys!(SYS_PRESENT, 0u64, DATA_BASE, rect);
            if r != E_KIND {
                bad += 1;
            }
            i += 1;
            if i % 2000 == 0 {
                sys!(thread::SYS_YIELD, 0u64, 0u64, 0u64);
            }
        }
        sys!(SYS_REPORT, 0u64, 203u64, bad);
        // Pong's framebuffer lives above DATA_BASE in Pong's space. Here that
        // address is unmapped: the read faults and the thread dies.
        let v: u64;
        asm!("mov {0}, qword ptr [{1}]", out(reg) v, in(reg) DATA_BASE + 0x1000, options(nostack));
        sys!(SYS_REPORT, 0u64, 204u64, v);
        asm!("mov qword ptr [{0}], 0xBAD", in(reg) DATA_BASE + 0x1000, options(nostack));
        sys!(thread::SYS_EXIT, 1u64, 0u64, 0u64);
        loop {}
    }
}

/// Tries to manufacture a display capability: mint from the console (no
/// GRANT), then every slot it could name. Exits 0 after reporting.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_forger() -> ! {
    unsafe {
        let r = sys!(thread::SYS_MINT, 0u64, 1u64, 0x1Fu64);
        sys!(SYS_REPORT, 0u64, 210u64, r);
        let mut refused = 0u64;
        let mut s = 0u64;
        while s < 16 {
            let r = sys!(SYS_DISPLAY_INFO, s, 0u64, 0u64);
            // Any negative result is a refusal. (Written as one sign test so the
            // compiler emits no jump table in kernel rodata, which ring 3 cannot read.)
            if (r as i64) < 0 {
                refused += 1;
            }
            s += 1;
        }
        sys!(SYS_REPORT, 0u64, 211u64, refused);
        let r = sys!(SYS_TICKS, 0u64, 0u64, 0u64);
        sys!(SYS_REPORT, 0u64, 212u64, r);
        sys!(thread::SYS_EXIT, 0u64, 0u64, 0u64);
        loop {}
    }
}

/// Burns CPU forever; the partition timer must kill it, not the game.
#[link_section = ".user"]
#[inline(never)]
pub extern "C" fn hostile_spinner() -> ! {
    loop {
        unsafe { asm!("pause", options(nomem, nostack)) }
    }
}

// ---- boot and finish ----------------------------------------------------------------------------

fn map_rw_range(a: &mut AddressSpace, fa: &mut FrameAlloc, from: u64, to: u64) {
    let mut va = from;
    while va < to {
        if !a.is_mapped(va) {
            a.map_user(fa, va, Perm::Rw).expect("guest rw page");
        }
        va += mm::PAGE;
    }
}

/// Replaces the escape-suite setup in `kmain` when the feature is on.
pub fn boot(mem_upper_kib: u32) -> ! {
    kprintln!("pong: Warden display runtime (320x240x32 BGA, i8042 polled, syscalls 60..64)");
    let mem_end = 0x10_0000 + (mem_upper_kib as u64) * 1024;
    let mut frames = FrameAlloc::new(32 << 20, mem_end.min(1 << 30));
    arch::init();

    // Display.
    let disp = d();
    match find_vga_lfb() {
        Some(bar) => {
            bga_set_mode();
            let pd = frames.alloc().expect("lfb pd");
            fill_lfb_pd(pd, bar);
            map_lfb_into(core::ptr::addr_of!(boot_pml4) as u64, pd, bar);
            disp.lfb = bar;
            disp.real = true;
            unsafe { LFB_PD = pd };
            kprintln!("pong: VGA linear framebuffer at {bar:#x}, mapped supervisor-only");
        }
        None => {
            disp.lfb = core::ptr::addr_of_mut!(FAKE_LFB) as u64;
            kprintln!("pong: no VGA device; presenting into a kernel buffer (not visible)");
        }
    }
    unsafe {
        for p in 0..(FB_W * FB_H) as u64 {
            *((disp.lfb + p * 4) as *mut u32) = 0xFF00_0000;
        }
    }

    let mut k = Kernel {
        caps: warden_caps::CapTable::with_capacity(256, 128), frames, endpoints: alloc::collections::BTreeMap::new(),
        user_frames: alloc::collections::BTreeMap::new(),
        threads: alloc::vec::Vec::new(), current: 0, ticks: 0, failures: 0, checks: 0,
        cap_nodes_baseline: 0, heap_baseline: 0, heap_carved_baseline: 0, frames_baseline: 0,
        frames_high_water_baseline: 0, host_object: None, linux_run_frames: 0, ep_over_capacity: 0, ep_refused: 0,
        fx_owner: None, ts_set: false, fs_loaded: 0,
    };

    // Pong: the C guest. Code R-X at 0x800000, data/bss/heap RW+NX at 0x840000..0x9E0000, 64 KiB stack.
    let mut a = AddressSpace::new(&mut k.frames).expect("pong page tables");
    a.load(&mut k.frames, CODE_BASE, PONG_TEXT, Perm::Rx).expect("pong code");
    if !PONG_DATA.is_empty() {
        a.load(&mut k.frames, DATA_BASE, PONG_DATA, Perm::Rw).expect("pong data");
    }
    map_rw_range(&mut a, &mut k.frames, DATA_BASE, HEAP_END);
    map_rw_range(&mut a, &mut k.frames, STACK_LOW, STACK_TOP);
    if disp.real {
        map_lfb_into(a.cr3, unsafe { LFB_PD }, disp.lfb);
    }
    let pong_frames = k.frames.used;
    let pong = k.spawn(if cfg!(feature = "game_probe") { "game-probe" } else { "pong" }, CODE_BASE, STACK_TOP, 50, Death::Exited(0), a);
    unsafe { PONG_THREAD = pong };
    thread::root_endpoint(&mut k, pong, 0, Rights::WRITE, 1);
    disp.object = Some(thread::root_cap(&mut k, pong, 1, ObjectKind::Frame, Rights::READ.union(Rights::WRITE), 0));

    // Hostiles: the kernel image's own ring-3 code, each in a private space with
    // a console capability and nothing else.
    let (us, ue) = (core::ptr::addr_of!(__user_start) as u64, core::ptr::addr_of!(__user_end) as u64);
    let user_code = unsafe { core::slice::from_raw_parts(us as *const u8, (ue - us) as usize) };
    let hostiles: [(&'static str, extern "C" fn() -> !, Death, u32); 3] = [
        ("hostile writer", hostile_writer, Death::Fault(14), 50),
        ("hostile forger", hostile_forger, Death::Exited(0), 50),
        ("hostile spinner", hostile_spinner, Death::Budget, 5),
    ];
    for (name, entry, expect, budget) in hostiles {
        let mut a = AddressSpace::new(&mut k.frames).expect("page tables");
        a.load(&mut k.frames, HOSTILE_CODE, user_code, Perm::Rx).expect("user code");
        for i in 0..16 {
            a.map_user(&mut k.frames, STACK_TOP - (i + 1) * mm::PAGE, Perm::Rw).expect("stack");
        }
        if disp.real {
            map_lfb_into(a.cr3, unsafe { LFB_PD }, disp.lfb);
        }
        let t = k.spawn(name, entry as usize as u64, STACK_TOP, budget, expect, a);
        thread::root_endpoint(&mut k, t, 0, Rights::WRITE, 1);
    }
    k.cap_nodes_baseline = k.caps.len();
    k.heap_baseline = heap::used();
    k.heap_carved_baseline = heap::carved();
    kprintln!("pong: {} threads in {} address spaces; pong's space uses {} frames ({} KiB); {} frames total; kernel heap {} bytes",
        k.threads.len(), k.threads.len(), pong_frames, pong_frames * 4, k.frames.used, heap::used());
    kprintln!("pong: ready");
    unsafe { *core::ptr::addr_of_mut!(thread::KERNEL) = Some(k) };
    thread::start()
}

static mut LFB_PD: u64 = 0;

fn lfb_hash() -> u64 {
    let disp = d();
    let mut h: u64 = 1469598103934665603;
    for p in 0..(FB_W * FB_H) as u64 {
        let v = unsafe { *((disp.lfb + p * 4) as *const u32) };
        for b in v.to_le_bytes() {
            h ^= b as u64;
            h = h.wrapping_mul(1099511628211);
        }
    }
    h
}

fn check(k: &mut Kernel, ok: bool, what: &str) {
    k.checks += 1;
    if !ok {
        k.failures += 1;
    }
    kprintln!("{} {what}", if ok { "PASS" } else { "FAIL" });
}

/// Replaces `finish` when the feature is on.
pub fn finish(k: &mut Kernel) -> ! {
    let pong = unsafe { PONG_THREAD };
    let (acc, refused, bytes) = unsafe { (PRESENTS[pong][0], PRESENTS[pong][1], PRESENTS[pong][2]) };
    let hostile_acc: u64 = (0..k.threads.len()).filter(|&i| i != pong).map(|i| unsafe { PRESENTS[i.min(MAX_THREADS - 1)][0] }).sum();
    let hostile_ref: u64 = (0..k.threads.len()).filter(|&i| i != pong).map(|i| unsafe { PRESENTS[i.min(MAX_THREADS - 1)][1] }).sum();
    #[cfg(feature = "game_probe")]
    {
        // Exact PR #68 renderer, executed in ring 3 via capability-granted
        // SYS_PRESENT. Never infer Doom compatibility from this workload.
        check(k, report(40) == Some(60), "game-probe completed 60 ring-3 frames");
        check(k, report(41).is_some(), "game-probe reported a frame digest");
        check(k, acc == 60, "game-probe sent 60 framebuffer presents");
        check(k, hostile_acc == 0 && hostile_ref > 0, "hostile present attempts were denied");
        check(k, k.threads[pong].death == Some(Death::Exited(0)), "game-probe exited normally");
        kprintln!("game-probe: reported digest {:?}, {} accepted presents, {} hostile presents refused", report(41), acc, hostile_ref);
        kprintln!("{}: {} checks, {} failed", if k.failures == 0 { "ALL HELD" } else { "GAME PROBE FAILED" }, k.checks, k.failures);
        arch::qemu_exit(if k.failures == 0 { 0 } else { 1 });
    }
    kprintln!();
    kprintln!("pong: {acc} tile presents accepted ({} KiB to the screen), {refused} refused; hostile presents accepted {hostile_acc}, refused {hostile_ref}; keys seen {}",
        bytes / 1024, unsafe { KEYS_SEEN });
    if let (Some(f), Some(fps), Some(tpf), Some(worst), Some(late)) = (report(10), report(11), report(12), report(13), report(14)) {
        kprintln!("pong: phase A, gameplay paced to 30 fps for 3 s: {f} frames = {}.{:02} fps, {tpf} tiles per frame, worst frame {worst} us, {late} frames missed their 33 ms slot", fps / 100, fps % 100);
    }
    if let (Some(f), Some(fps), Some(tpf), Some(worst)) = (report(20), report(21), report(22), report(23)) {
        kprintln!("pong: phase B, gameplay uncapped for 2 s: {f} frames = {}.{:02} fps, {tpf} tiles per frame, worst frame {worst} us", fps / 100, fps % 100);
    }
    if let (Some(f), Some(fps), Some(tpf), Some(worst)) = (report(25), report(26), report(27), report(28)) {
        kprintln!("pong: phase C, full-screen redraw uncapped for 2 s: {f} frames = {}.{:02} fps, {tpf} tiles per frame, worst frame {worst} us", fps / 100, fps % 100);
    }
    if let Some(tsc) = report(2) {
        kprintln!("pong: TSC {} MHz by the partition timer", tsc / 10_000);
    }
    check(k, hostile_acc == 0, "no hostile thread ever put a pixel on the screen");
    check(k, acc > 0, "pong presented frames");
    check(k, report(10).unwrap_or(0) > 0 && report(20).unwrap_or(0) > 0 && report(25).unwrap_or(0) > 0, "pong completed all three timed phases");
    check(k, report(14) == Some(0), "paced phase: no frame missed its 33 ms slot");
    let screen = lfb_hash();
    let believed = report(31).unwrap_or(1);
    check(k, screen == believed, "the screen holds exactly the frame Pong last presented (hash match)");
    let refused = |v: Option<u64>| matches!(v, Some(x) if x == E_KIND || x == E_RIGHTS);
    check(k, report(200) == Some(E_INVALID) && report(201) == Some(E_KIND) && refused(report(202)),
        "hostile writer: present via empty slot, present and key via console cap all refused");
    check(k, report(203) == Some(0), "hostile writer: 20000 presents refused, none slipped through");
    check(k, report(204).is_none(), "hostile writer: died on the first touch of Pong's addresses, before reporting");
    check(k, report(210) == Some(E_RIGHTS) && report(211) == Some(16) && refused(report(212)),
        "hostile forger: cannot mint or name a display capability from any slot");
    // The heap frees now, so `used` can shrink; what a hostile thread could
    // make the kernel take from the arena is the carved amount.
    const HEAP_LIMIT: usize = 4096;
    let grew = heap::carved().saturating_sub(k.heap_carved_baseline);
    check(k, grew <= HEAP_LIMIT, "kernel heap carved during the run within 4 KiB");
    check(k, k.caps.len() == k.cap_nodes_baseline, "capability table unchanged: no capability was created or copied");
    kprintln!("pong: screen hash {screen:#018x}, kernel heap carved {grew} more bytes, {} frames ({} KiB) of physical memory in use, {} timer ticks",
        k.frames.used, k.frames.used * 4, k.ticks);
    kprintln!();
    kprintln!("{}: {} checks, {} failed", if k.failures == 0 { "ALL HELD" } else { "PONG RUN FAILED" }, k.checks, k.failures);
    arch::qemu_exit(if k.failures == 0 { 0 } else { 1 })
}
