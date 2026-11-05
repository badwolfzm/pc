//! The kernel's heap never reclaims memory, so any capability operation that
//! allocates on a path user code can repeat is a resource-exhaustion attack.
//! This test wraps the system allocator and counts allocations while a
//! hostile pattern of 10,000 revocations runs. The number must stay zero.

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};
use warden_caps::{CSpace, CapError, CapTable, ObjectKind, Rights};

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
}

#[global_allocator]
static GLOBAL: Counting = Counting;

#[test]
fn ten_thousand_revocations_allocate_nothing() {
    let mut t = CapTable::new();
    let mut cs = CSpace::new(8);
    t.create_root(&mut cs, 0, ObjectKind::Dir, Rights::ALL, 0).unwrap();
    t.create_root(&mut cs, 1, ObjectKind::Untyped, Rights::ALL, 1 << 20).unwrap();
    // Three generations below slot 0 so the first revoke has real work.
    let mut child = CSpace::new(2);
    t.mint(&cs, 0, &mut child, 0, Rights::ALL, 1).unwrap();
    let mut grandchild = CSpace::new(2);
    t.mint(&child, 0, &mut grandchild, 0, Rights::ALL, 2).unwrap();
    let mut sibling = CSpace::new(2);
    t.mint(&cs, 0, &mut sibling, 0, Rights::READ, 3).unwrap();

    let before = ALLOCS.load(Ordering::SeqCst);
    assert_eq!(t.revoke(&cs, 0), Ok(3));
    for _ in 0..10_000 {
        assert_eq!(t.revoke(&cs, 0), Ok(0)); // subtree already dead
        assert_eq!(t.revoke(&cs, 1), Ok(0)); // no children at all
        assert_eq!(t.revoke(&child, 0), Err(CapError::Revoked)); // through a dead cap
        assert_eq!(t.revoke(&cs, 5), Err(CapError::InvalidSlot)); // forged slot
    }
    let after = ALLOCS.load(Ordering::SeqCst);
    assert_eq!(after, before, "revoke allocated {} times", after - before);
    assert_eq!(t.invoke(&grandchild, 0, Rights::NONE), Err(CapError::Revoked));
}
