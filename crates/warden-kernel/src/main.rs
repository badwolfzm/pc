//! Warden microkernel, Phase 1.
#![no_std]
#![no_main]
#![cfg_attr(any(feature = "pong", feature = "game_probe"), allow(unreachable_code, unused_variables))]

extern crate alloc;

mod apps;
mod arch;
mod heap;
mod linux;
mod loader;
mod mm;
mod pong;
mod serial;
mod tests;
mod thread;

use thread::Kernel;
use warden_caps::{ObjectKind, Rights};

#[global_allocator]
static ALLOC: heap::Heap = heap::Heap;

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
    kprintln!("warden kernel phase 3 (x86-64, no device interrupts, one partition timer, untyped memory)");
    if magic != 0x2BADB002 {
        kprintln!("not booted by a multiboot loader (magic {magic:#x})");
        arch::qemu_exit(2);
    }
    let info = unsafe { &*(mbi as *const MultibootInfo) };
    let (lo, hi) = (info.mem_lower, info.mem_upper);
    #[cfg(any(feature = "pong", feature = "game_probe"))]
    pong::boot(hi);
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
    let mut frames = mm::FrameAlloc::new(32 << 20, mem_end.min(1 << 30));
    let (us, ue) = (core::ptr::addr_of!(__user_start) as u64, core::ptr::addr_of!(__user_end) as u64);
    kprintln!("kernel ends at {kend:#x}; user partition code {us:#x}..{ue:#x} in page {:#x}..{:#x}", tests::USER_BASE, tests::USER_END);
    assert!(us >= tests::USER_BASE && ue <= tests::USER_BASE + (1 << 20), "user code must sit in the user page");

    arch::init();
    arch::init_syscall();

    // Review finding: a refused mapping must not consume a frame. The mm
    // self-test checks that and the rest of the mapping rules (W^X bits,
    // user copies, teardown), one PASS/FAIL line each.
    let (mm_checks, mm_fails) = mm::self_test(&mut frames);
    let user_code = unsafe { core::slice::from_raw_parts(us as *const u8, (ue - us) as usize) };
    let mut k = Kernel {
        caps: warden_caps::CapTable::with_capacity(256, 128), frames, endpoints: alloc::collections::BTreeMap::new(), ep_over_capacity: 0, ep_refused: 0,
        user_frames: alloc::collections::BTreeMap::new(),
        threads: alloc::vec::Vec::new(), current: 0, ticks: 0, failures: 0, checks: 0,
        cap_nodes_baseline: 0, heap_baseline: 0, heap_carved_baseline: 0, frames_baseline: 0,
        frames_high_water_baseline: 0, host_object: None, linux_run_frames: 0,
        fx_owner: None, ts_set: false, fs_loaded: 0,
    };
    k.checks += mm_checks;
    k.failures += mm_fails;
    // One address space per thread: a private copy of the user code, one
    // private data page, and a 64 KiB stack, all at the same virtual
    // addresses in every thread.
    for c in tests::CASES {
        let mut a = mm::AddressSpace::new(&mut k.frames).expect("page tables");
        // W^X: code is read-only and executable, data and stack never execute.
        a.load(&mut k.frames, tests::USER_BASE, user_code, mm::Perm::Rx).expect("user code");
        a.map_user(&mut k.frames, tests::DATA_PAGE, mm::Perm::Rw).expect("data page");
        for i in 0..16 {
            a.map_user(&mut k.frames, tests::STACK_TOP - (i + 1) * mm::PAGE, mm::Perm::Rw).expect("stack");
        }
        let t = k.spawn(c.name, c.entry as usize as u64, tests::STACK_TOP, c.budget, c.expect, a);
        thread::root_endpoint(&mut k, t, 0, Rights::WRITE, 1);
        thread::root_cap(&mut k, t, 1, ObjectKind::Dir, Rights::READ.union(Rights::GRANT), 0);
        // Each thread's untyped memory: everything it creates is paid from it.
        thread::root_cap(&mut k, t, 3, ObjectKind::Untyped, Rights::ALL, c.untyped);
        // Slot 4, the host capability, is minted for nobody.
    }
    // One shared endpoint: the server holds the root, the client a WRITE
    // copy badged 42, the eavesdropper a WRITE copy badged 7.
    let server = k.threads.iter().position(|t| t.name == "ipc server").unwrap();
    let client = k.threads.iter().position(|t| t.name == "ipc client").unwrap();
    let eaves = k.threads.iter().position(|t| t.name == "ipc eavesdropper").unwrap();
    // The server creates it and pays for it, queues included, from its own
    // untyped memory (capacity 4 per direction). The client's copy carries
    // GRANT so it can try to copy and re-badge it.
    let before = (k.caps.len(), k.caps.live_objects(), heap::used(), k.endpoints.len());
    let refused = thread::create_endpoint(&mut k, server, 3, 6, warden_caps::ENDPOINT_MAX_QUEUE + 1);
    k.checks += 1;
    if refused == Err(warden_caps::CapError::OutOfBudget) && before == (k.caps.len(), k.caps.live_objects(), heap::used(), k.endpoints.len())
        && k.caps.budget(&k.threads[server].cspace, 3) == Ok(1 << 20) {
        kprintln!("PASS endpoint with an over-budget queue refused: nothing allocated or charged");
    } else {
        k.failures += 1;
        kprintln!("FAIL over-budget endpoint creation: {refused:?}, state before {before:?}");
    }
    thread::create_endpoint(&mut k, server, 3, 6, 4).expect("ipc endpoint");
    thread::mint_to(&mut k, server, 6, client, 6, Rights::WRITE.union(Rights::GRANT), 42);
    thread::mint_to(&mut k, server, 6, eaves, 6, Rights::WRITE, 7);
    // Flood target: an endpoint nobody ever receives on, with room for two
    // waiting senders. Three threads send; the third must be refused, not
    // queued in freshly allocated kernel memory.
    let flood: alloc::vec::Vec<usize> = ["flood sender 1", "flood sender 2", "flood sender 3"]
        .iter().map(|n| k.threads.iter().position(|t| t.name == *n).unwrap()).collect();
    thread::create_endpoint(&mut k, flood[0], 3, 8, 2).expect("flood endpoint");
    thread::mint_to(&mut k, flood[0], 8, flood[1], 8, Rights::WRITE, 0);
    thread::mint_to(&mut k, flood[0], 8, flood[2], 8, Rights::WRITE, 0);
    // A second endpoint for the revocation test: the revoker holds the root,
    // the victim a READ|WRITE copy badged 9 that the revoker kills first.
    let revoker = k.threads.iter().position(|t| t.name == "ipc revoker").unwrap();
    let victim = k.threads.iter().position(|t| t.name == "ipc victim").unwrap();
    thread::create_endpoint(&mut k, revoker, 3, 9, 1).expect("revocation endpoint");
    thread::mint_to(&mut k, revoker, 9, victim, 9, Rights::READ.union(Rights::WRITE), 9);
    // Phase 2b: the personality server and the three applications.
    apps::setup(&mut k);
    // Each creator paid for its endpoint (object plus queues at the chosen
    // capacity, plus one node) and for every copy minted from it; a holder
    // paid nothing. The queue budget is ordinary untyped accounting.
    let price = |cap: u64, copies: u64| (1u64 << 20) - warden_caps::endpoint_bytes(cap) - (1 + copies) * warden_caps::NODE_BYTES;
    let left = |k: &Kernel, t: usize| k.caps.budget(&k.threads[t].cspace, 3);
    let got = (left(&k, server), left(&k, flood[0]), left(&k, revoker), left(&k, client));
    let want = (Ok(price(4, 2)), Ok(price(2, 2)), Ok(price(1, 1)), Ok(1 << 20));
    k.checks += 1;
    if got == want {
        kprintln!("PASS endpoint creators paid for their queues from untyped memory ({}, {} and {} bytes); holders paid nothing",
            (1u64 << 20) - price(4, 2), (1u64 << 20) - price(2, 2), (1u64 << 20) - price(1, 1));
    } else {
        k.failures += 1;
        kprintln!("FAIL endpoint charges: got {got:?}, want {want:?}");
    }
    k.cap_nodes_baseline = k.caps.len();
    k.heap_baseline = heap::used();
    k.heap_carved_baseline = heap::carved();
    k.frames_baseline = k.frames.used;
    k.frames_high_water_baseline = k.frames.high_water();
    kprintln!("{} ring-3 threads in {} address spaces, {} frames ({} KiB) allocated, {} bytes of kernel heap in use ({} carved); starting the partition timer",
        k.threads.len(), k.threads.len(), k.frames.used, k.frames.used * 4, heap::used(), heap::carved());
    unsafe { *core::ptr::addr_of_mut!(thread::KERNEL) = Some(k) };
    // Interrupts stay off in the kernel; each user frame carries IF set.
    thread::start()
}

pub fn finish(k: &mut Kernel) -> ! {
    #[cfg(any(feature = "pong", feature = "game_probe"))]
    pong::finish(k);
    let expected_logs = tests::EXPECT.len() as u32;
    let logs: u32 = k.threads.iter().map(|t| t.logs).sum();
    k.checks += 1;
    if logs != expected_logs {
        k.failures += 1;
        kprintln!("FAIL threads reported {logs} of {expected_logs} expected results");
    } else {
        kprintln!("PASS threads reported all {expected_logs} expected results");
    }
    // Every thread has died and been torn down: nothing it created may
    // survive it. Capabilities, objects, user frames and the heap all go
    // back to where they were (the heap may keep a few freed-list blocks
    // and the frame free list's own vector).
    k.checks += 1;
    let (nodes, objects) = (k.caps.live_nodes(), k.caps.live_objects());
    if nodes == 0 && objects == 0 && k.user_frames.is_empty() && k.endpoints.is_empty() {
        kprintln!("PASS every capability and object was reclaimed with its holders (0 live nodes, 0 live objects)");
    } else {
        k.failures += 1;
        kprintln!("FAIL {} live nodes, {} live objects, {} user frames, {} endpoint queues survive their holders", nodes, objects, k.user_frames.len(), k.endpoints.len());
    }
    k.checks += 1;
    const ARENA_LIMIT: usize = 8;
    let arena_grew = k.caps.len() - k.cap_nodes_baseline;
    if arena_grew <= ARENA_LIMIT {
        kprintln!("PASS capability node arena grew by {arena_grew} entries during the run (limit {ARENA_LIMIT}; freed entries were reused)");
    } else {
        k.failures += 1;
        kprintln!("FAIL capability node arena grew by {arena_grew} entries (limit {ARENA_LIMIT})");
    }
    k.checks += 1;
    if k.frames.used == k.frames_baseline {
        kprintln!("PASS all user frames returned: {} frames in use, as after setup", k.frames.used);
    } else {
        k.failures += 1;
        kprintln!("FAIL {} frames in use, {} after setup", k.frames.used, k.frames_baseline);
    }
    k.checks += 1;
    const FRAME_PEAK: u64 = 8;
    // Linux programs grow a heap and mmap during the run; those frames are
    // back in the pool (checked above) but were taken fresh.
    let fresh = (k.frames.high_water() - k.frames_high_water_baseline).saturating_sub(k.linux_run_frames);
    if fresh <= FRAME_PEAK {
        kprintln!("PASS {fresh} fresh frames taken during the run beyond the {} Linux heap/mmap pages (limit {FRAME_PEAK}; freed frames were reused)", k.linux_run_frames);
    } else {
        k.failures += 1;
        kprintln!("FAIL {fresh} fresh frames taken during the run beyond the {} Linux heap/mmap pages (limit {FRAME_PEAK})", k.linux_run_frames);
    }
    // Net heap: thousands of create/delete cycles ran; what remains is the
    // allocator's own bookkeeping (the frame free-list vector).
    const HEAP_LIMIT: usize = 1024;
    const CARVE_LIMIT: usize = 4096;
    let grew = heap::used() as isize - k.heap_baseline as isize;
    let carved = heap::carved() - k.heap_carved_baseline;
    k.checks += 1;
    if grew <= HEAP_LIMIT as isize {
        kprintln!("PASS kernel heap in use grew {grew} bytes net during the run (limit {HEAP_LIMIT})");
    } else {
        k.failures += 1;
        kprintln!("FAIL kernel heap in use grew {grew} bytes net during the run (limit {HEAP_LIMIT})");
    }
    k.checks += 1;
    if carved <= CARVE_LIMIT {
        kprintln!("PASS kernel heap high-water mark grew {carved} bytes during the run (limit {CARVE_LIMIT})");
    } else {
        k.failures += 1;
        kprintln!("FAIL kernel heap high-water mark grew {carved} bytes during the run (limit {CARVE_LIMIT})");
    }
    // Recorded before the endpoints were reclaimed with their holders: no
    // queue ever held more threads than its creator paid for, and exactly the
    // one sender over capacity was turned away (test 29).
    k.checks += 1;
    if k.ep_over_capacity == 0 && k.ep_refused == 1 {
        kprintln!("PASS no endpoint queue exceeded its paid capacity; {} over-capacity IPC call refused", k.ep_refused);
    } else {
        k.failures += 1;
        kprintln!("FAIL {} endpoint queues exceeded capacity, {} IPC calls refused (want 1)", k.ep_over_capacity, k.ep_refused);
    }
    kprintln!();
    kprintln!("{}: {} checks, {} failed, {} timer ticks, {} frames, heap {} bytes in use ({} carved)", if k.failures == 0 { "ALL HELD" } else { "KERNEL TEST FAILED" }, k.checks, k.failures, k.ticks, k.frames.used, heap::used(), heap::carved());
    arch::qemu_exit(if k.failures == 0 { 0 } else { 1 })
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    kprintln!("KERNEL PANIC: {info}");
    arch::qemu_exit(2)
}
