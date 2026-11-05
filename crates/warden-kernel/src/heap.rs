//! Kernel heap: size-class free lists over a fixed arena.
//!
//! Phase 1 used a bump allocator that never freed; the budget check was what
//! kept that safe. Phase 3 gives every kernel object an owner who pays for it
//! from untyped memory, and when an object is freed its bytes come back. For
//! that to mean anything the heap must actually reuse memory, so this is a
//! real allocator: requests are rounded up to a power-of-two class (16 B to
//! 4 KiB), each class keeps a singly linked free list threaded through the
//! free blocks, and a class with an empty list carves a new block from the
//! bump pointer. Larger requests are carved directly and never reused (the
//! kernel makes none after boot; `finish` would notice if it did).
//!
//! `used()` is the net number of bytes in live blocks, so the in-guest check
//! "the heap is back where it started after the run" is meaningful.

use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::ptr;
use core::sync::atomic::{AtomicUsize, Ordering};

const ARENA: usize = 1 << 20;
const MIN_CLASS: usize = 16;
const MAX_CLASS: usize = 4096;
const CLASSES: usize = 9; // 16, 32, ..., 4096

#[repr(align(4096))]
struct Arena(UnsafeCell<[u8; ARENA]>);
unsafe impl Sync for Arena {}

static ARENA_MEM: Arena = Arena(UnsafeCell::new([0; ARENA]));
/// Bump pointer: bytes of the arena ever carved out.
static NEXT: AtomicUsize = AtomicUsize::new(0);
/// Net bytes in live blocks.
static USED: AtomicUsize = AtomicUsize::new(0);

struct Lists(UnsafeCell<[*mut u8; CLASSES]>);
unsafe impl Sync for Lists {}
static FREE: Lists = Lists(UnsafeCell::new([ptr::null_mut(); CLASSES]));

pub struct Heap;

fn class_of(l: Layout) -> Option<usize> {
    let need = l.size().max(l.align()).max(MIN_CLASS);
    if need > MAX_CLASS {
        return None;
    }
    Some(need.next_power_of_two().trailing_zeros() as usize - MIN_CLASS.trailing_zeros() as usize)
}

/// Carve `size` bytes aligned to `align` from the bump region.
unsafe fn carve(size: usize, align: usize) -> *mut u8 {
    let mut cur = NEXT.load(Ordering::Relaxed);
    loop {
        let start = (cur + align - 1) & !(align - 1);
        let end = match start.checked_add(size) {
            Some(e) if e <= ARENA => e,
            _ => return ptr::null_mut(),
        };
        match NEXT.compare_exchange(cur, end, Ordering::Relaxed, Ordering::Relaxed) {
            Ok(_) => return (ARENA_MEM.0.get() as *mut u8).add(start),
            Err(c) => cur = c,
        }
    }
}

// The kernel runs on one CPU with interrupts off while in the kernel, so the
// free lists need no lock; the atomics above only document intent.
unsafe impl GlobalAlloc for Heap {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        match class_of(l) {
            Some(c) => {
                let size = MIN_CLASS << c;
                let lists = &mut *FREE.0.get();
                let p = if lists[c].is_null() {
                    carve(size, size)
                } else {
                    let p = lists[c];
                    lists[c] = *(p as *mut *mut u8);
                    p
                };
                if !p.is_null() {
                    USED.fetch_add(size, Ordering::Relaxed);
                }
                p
            }
            None => {
                let p = carve(l.size(), l.align());
                if !p.is_null() {
                    USED.fetch_add(l.size(), Ordering::Relaxed);
                }
                p
            }
        }
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        match class_of(l) {
            Some(c) => {
                let lists = &mut *FREE.0.get();
                *(p as *mut *mut u8) = lists[c];
                lists[c] = p;
                USED.fetch_sub(MIN_CLASS << c, Ordering::Relaxed);
            }
            None => {
                // Large blocks are not reused; the accounting still drops.
                USED.fetch_sub(l.size(), Ordering::Relaxed);
            }
        }
    }
}

/// Net bytes in live heap blocks.
pub fn used() -> usize {
    USED.load(Ordering::Relaxed)
}

/// Bytes of the arena ever carved out (high-water mark).
pub fn carved() -> usize {
    NEXT.load(Ordering::Relaxed)
}
