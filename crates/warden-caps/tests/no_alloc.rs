//! Steady-state operations allocate nothing.
//!
//! Wraps the system allocator in a counter and runs 10,000 rounds of every
//! operation a thread can issue (mint, revoke, delete, retype, budget, refused
//! variants of each) on a table whose arenas were sized up front. The
//! kernel's heap is a fixed arena; a hostile thread must not be able to grow
//! kernel memory by issuing requests, successful or refused.

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};
use warden_caps::{CSpace, CapError, CapTable, ObjectKind, Rights, NODE_BYTES};

struct Counting;
static ALLOCS: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        ALLOCS.fetch_add(1, Ordering::SeqCst);
        System.alloc(l)
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        System.dealloc(p, l)
    }
    unsafe fn realloc(&self, p: *mut u8, l: Layout, n: usize) -> *mut u8 {
        ALLOCS.fetch_add(1, Ordering::SeqCst);
        System.realloc(p, l, n)
    }
}

#[global_allocator]
static A: Counting = Counting;

#[test]
fn steady_state_operations_allocate_nothing() {
    let mut t = CapTable::with_capacity(64, 32);
    let mut cs = CSpace::new(16);
    let mut other = CSpace::new(4);
    t.create_root(&mut cs, 0, ObjectKind::Dir, Rights::READ.union(Rights::GRANT), 0).unwrap();
    t.create_root(&mut cs, 1, ObjectKind::Untyped, Rights::ALL, 3 * (4096 + NODE_BYTES)).unwrap();
    // Warm up: touch every path once so the arenas hold their peak.
    t.retype(&mut cs, 1, ObjectKind::Frame, 0, 2).unwrap();
    t.mint(&cs, 2, &mut other, 0, Rights::READ, 0).unwrap();
    t.delete(&mut cs, 2).unwrap();
    t.delete(&mut other, 0).unwrap();
    assert!(t.take_freed().is_some());

    let before = ALLOCS.load(Ordering::SeqCst);
    for _ in 0..10_000 {
        // Create, copy, revoke, delete.
        let (_, rem) = t.retype(&mut cs, 1, ObjectKind::Frame, 0, 2).unwrap();
        assert_eq!(rem, 2 * (4096 + NODE_BYTES));
        t.mint(&cs, 2, &mut other, 0, Rights::READ.union(Rights::MAP), 0).unwrap();
        t.mint_self(&mut cs, 2, 3, Rights::READ, 0).unwrap();
        assert_eq!(t.revoke(&cs, 2).unwrap(), 2);
        assert_eq!(t.delete(&mut cs, 2).unwrap(), 1);
        assert!(t.take_freed().is_some());
        assert_eq!(t.take_freed(), None);
        // Refused variants.
        assert_eq!(t.mint(&cs, 0, &mut other, 9, Rights::ALL, 0), Err(CapError::InvalidSlot));
        assert_eq!(t.mint(&cs, 5, &mut other, 0, Rights::ALL, 0), Err(CapError::InvalidSlot));
        assert_eq!(t.revoke(&cs, 7), Err(CapError::InvalidSlot));
        assert_eq!(t.revoke(&other, 0), Err(CapError::Revoked));
        assert_eq!(t.delete(&mut cs, 2), Err(CapError::InvalidSlot));
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Untyped, 1 << 20, 2), Err(CapError::OutOfBudget));
        assert_eq!(t.retype(&mut cs, 0, ObjectKind::Frame, 0, 2), Err(CapError::WrongKind));
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Frame, 0, 1), Err(CapError::SlotOccupied));
        assert_eq!(t.budget(&cs, 0), Err(CapError::WrongKind));
        assert_eq!(t.budget(&cs, 1).unwrap(), 3 * (4096 + NODE_BYTES));
    }
    assert_eq!(ALLOCS.load(Ordering::SeqCst) - before, 0, "operations allocated");
    assert_eq!(t.len(), 5, "node arena at its peak: two roots, the frame, two copies");
}
