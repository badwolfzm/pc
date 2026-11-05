//! Warden microkernel, Phase 1.
#![no_std]
#![no_main]

extern crate alloc;

mod arch;
mod heap;
mod mm;
mod serial;
mod tests;
mod thread;

use thread::Kernel;
use warden_caps::{ObjectKind, Rights};

#[global_allocator]
static ALLOC: heap::Bump = heap::Bump;

#[repr(C, packed)]
struct MultibootInfo {
    flags: u32,
    mem_lower: u32,
    mem_upper: u32,
    _boot_device: u32,
    _cmdline: u32,
    _mods_count: u32,
    _mods_addr: u32,
    _syms: [u32; 4],
    mmap_length: u32,
    mmap_addr: u32,
}

extern "C" {
    static __kernel_end: u8;
    static __user_start: u8;
    static __user_end: u8;
}

#[no_mangle]
pub extern "C" fn kmain(mbi: u64, magic: u64) -> ! {
    serial::init();
    kprintln!();
    kprintln!("warden kernel phase 1 (x86-64, no device interrupts, one partition timer)");
    if magic != 0x2BADB002 {
        kprintln!("not booted by a multiboot loader (magic {magic:#x})");
        arch::qemu_exit(2);
    }
    let info = unsafe { &*(mbi as *const MultibootInfo) };
    let (lo, hi) = (info.mem_lower, info.mem_upper);
    kprintln!("memory: {} KiB low, {} MiB high", lo, hi / 1024);
    if info.flags & (1 << 6) != 0 {
        let (mut p, end) = (info.mmap_addr as u64, (info.mmap_addr + info.mmap_length) as u64);
        while p < end {
            // Multiboot entries are packed, so read them unaligned.
            let rd = |off: u64| -> u64 { unsafe { core::ptr::read_unaligned((p + off) as *const u64) } };
            let size = unsafe { core::ptr::read_unaligned(p as *const u32) } as u64;
            let (base, len) = (rd(4), rd(12));
            let ty = unsafe { core::ptr::read_unaligned((p + 20) as *const u32) };
            kprintln!("  region {:#012x} + {:#012x} type {}", base, len, ty);
            p += size + 4;
        }
    }
    let kend = core::ptr::addr_of!(__kernel_end) as u64;
    // Physical frames for page tables and user memory: 32 MiB up to the end
    // of the high memory QEMU reported.
    let mem_end = 0x10_0000 + (hi as u64) * 1024;
    let frames = mm::FrameAlloc::new(32 << 20, mem_end.min(1 << 30));
    let (us, ue) = (core::ptr::addr_of!(__user_start) as u64, core::ptr::addr_of!(__user_end) as u64);
    kprintln!("kernel ends at {kend:#x}; user partition code {us:#x}..{ue:#x} in page {:#x}..{:#x}", tests::USER_BASE, tests::USER_END);
    assert!(us >= tests::USER_BASE && ue <= tests::USER_BASE + (1 << 20), "user code must sit in the user page");

    arch::init();

    let user_code = unsafe { core::slice::from_raw_parts(us as *const u8, (ue - us) as usize) };
    let mut k = Kernel {
        caps: warden_caps::CapTable::new(), frames, endpoints: alloc::collections::BTreeMap::new(),
        threads: alloc::vec::Vec::new(), current: 0, ticks: 0, failures: 0, checks: 0,
    };
    // One address space per thread: a private copy of the user code, one
    // private data page, and a 64 KiB stack, all at the same virtual
    // addresses in every thread.
    for c in tests::CASES {
        let mut a = mm::AddressSpace::new(&mut k.frames).expect("page tables");
        a.load(&mut k.frames, mm::USER_BASE, user_code, true).expect("user code");
        a.map_user(&mut k.frames, tests::DATA_PAGE, true).expect("data page");
        for i in 0..16 {
            a.map_user(&mut k.frames, tests::STACK_TOP - (i + 1) * mm::PAGE, true).expect("stack");
        }
        let t = k.spawn(c.name, c.entry as usize as u64, tests::STACK_TOP, c.budget, c.expect, a);
        thread::root_cap(&mut k, t, 0, ObjectKind::Endpoint, Rights::WRITE, 0);
        thread::root_cap(&mut k, t, 1, ObjectKind::Dir, Rights::READ.union(Rights::GRANT), 0);
        thread::root_cap(&mut k, t, 3, ObjectKind::Untyped, Rights::ALL, 1 << 20);
        // Slot 4, the host capability, is minted for nobody.
    }
    // One shared endpoint: the server holds the root, the client a WRITE
    // copy badged 42, the eavesdropper a WRITE copy badged 7.
    let server = k.threads.iter().position(|t| t.name == "ipc server").unwrap();
    let client = k.threads.iter().position(|t| t.name == "ipc client").unwrap();
    let eaves = k.threads.iter().position(|t| t.name == "ipc eavesdropper").unwrap();
    thread::root_cap(&mut k, server, 6, ObjectKind::Endpoint, Rights::READ.union(Rights::WRITE).union(Rights::GRANT), 0);
    thread::mint_to(&mut k, server, 6, client, 6, Rights::WRITE, 42);
    thread::mint_to(&mut k, server, 6, eaves, 6, Rights::WRITE, 7);
    kprintln!("{} ring-3 threads in {} address spaces, {} frames ({} KiB) allocated, {} bytes of kernel heap; starting the partition timer",
        k.threads.len(), k.threads.len(), k.frames.used, k.frames.used * 4, heap::used());
    unsafe { *core::ptr::addr_of_mut!(thread::KERNEL) = Some(k) };
    // Interrupts stay off in the kernel; each user frame carries IF set.
    thread::start()
}

pub fn finish(k: &mut Kernel) -> ! {
    let expected_logs = tests::EXPECT.len() as u32;
    let logs: u32 = k.threads.iter().map(|t| t.logs).sum();
    k.checks += 1;
    if logs != expected_logs {
        k.failures += 1;
        kprintln!("FAIL threads reported {logs} of {expected_logs} expected results");
    } else {
        kprintln!("PASS threads reported all {expected_logs} expected results");
    }
    kprintln!();
    kprintln!("{}: {} checks, {} failed, {} timer ticks, {} frames, heap {} bytes", if k.failures == 0 { "ALL HELD" } else { "KERNEL TEST FAILED" }, k.checks, k.failures, k.ticks, k.frames.used, heap::used());
    arch::qemu_exit(if k.failures == 0 { 0 } else { 1 })
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    kprintln!("KERNEL PANIC: {info}");
    arch::qemu_exit(2)
}
