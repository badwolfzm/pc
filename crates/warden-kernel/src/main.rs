//! Warden microkernel, Phase 1.
#![no_std]
#![no_main]

extern crate alloc;

mod arch;
mod heap;
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
    let (us, ue) = (core::ptr::addr_of!(__user_start) as u64, core::ptr::addr_of!(__user_end) as u64);
    kprintln!("kernel ends at {kend:#x}; user partition code {us:#x}..{ue:#x} in page {:#x}..{:#x}", tests::USER_BASE, tests::USER_END);
    assert!(us >= tests::USER_BASE && ue <= tests::USER_BASE + (1 << 20), "user code must sit in the user page");

    arch::init();

    let mut k = Kernel { caps: warden_caps::CapTable::new(), threads: alloc::vec::Vec::new(), current: 0, ticks: 0, failures: 0, checks: 0 };
    // Each test thread gets a 64 KiB stack in the top half of the user page.
    let mut stack = tests::USER_END;
    for c in tests::CASES {
        stack -= 64 * 1024;
        let t = k.spawn(c.name, c.entry as usize as u64, stack, c.budget, c.expect);
        thread::root_cap(&mut k, t, 0, ObjectKind::Endpoint, Rights::WRITE, 0);
        thread::root_cap(&mut k, t, 1, ObjectKind::Dir, Rights::READ.union(Rights::GRANT), 0);
        thread::root_cap(&mut k, t, 3, ObjectKind::Untyped, Rights::ALL, 1 << 20);
        // Slot 4, the host capability, is minted for nobody.
    }
    kprintln!("{} ring-3 threads, {} bytes of kernel heap used; starting the partition timer", k.threads.len(), heap::used());
    unsafe { *core::ptr::addr_of_mut!(thread::KERNEL) = Some(k) };
    // Interrupts stay off in the kernel; each user frame carries IF set.
    thread::start()
}

pub fn finish(k: &mut Kernel) -> ! {
    let expected_logs = tests::EXPECT.len() as u32;
    let logs = k.threads[0].logs;
    k.checks += 1;
    if logs != expected_logs {
        k.failures += 1;
        kprintln!("FAIL capability thread reported {logs} of {expected_logs} results");
    } else {
        kprintln!("PASS capability thread reported all {expected_logs} results");
    }
    kprintln!();
    kprintln!("{}: {} checks, {} failed, {} timer ticks, heap {} bytes", if k.failures == 0 { "ALL HELD" } else { "KERNEL TEST FAILED" }, k.checks, k.failures, k.ticks, heap::used());
    arch::qemu_exit(if k.failures == 0 { 0 } else { 1 })
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    kprintln!("KERNEL PANIC: {info}");
    arch::qemu_exit(2)
}
