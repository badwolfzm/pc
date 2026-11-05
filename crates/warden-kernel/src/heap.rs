//! Kernel heap: a bump allocator over a fixed arena. The kernel has no heap
//! of its own to exhaust in the design; this arena is the Phase 1 stand-in,
//! sized so that a budget check, not the arena, is always what fails first.

use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};

const ARENA: usize = 1 << 20;

#[repr(align(4096))]
struct Arena(UnsafeCell<[u8; ARENA]>);
unsafe impl Sync for Arena {}

static ARENA_MEM: Arena = Arena(UnsafeCell::new([0; ARENA]));
static NEXT: AtomicUsize = AtomicUsize::new(0);

pub struct Bump;

unsafe impl GlobalAlloc for Bump {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        let mut cur = NEXT.load(Ordering::Relaxed);
        loop {
            let start = (cur + l.align() - 1) & !(l.align() - 1);
            let end = match start.checked_add(l.size()) {
                Some(e) if e <= ARENA => e,
                _ => return core::ptr::null_mut(),
            };
            match NEXT.compare_exchange(cur, end, Ordering::Relaxed, Ordering::Relaxed) {
                Ok(_) => return (ARENA_MEM.0.get() as *mut u8).add(start),
                Err(c) => cur = c,
            }
        }
    }
    unsafe fn dealloc(&self, _p: *mut u8, _l: Layout) {}
}

pub fn used() -> usize {
    NEXT.load(Ordering::Relaxed)
}
