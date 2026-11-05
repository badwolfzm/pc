//! Warden capability model.
//!
//! This crate is the object model the Warden microkernel will enforce. It is
//! `no_std` (needs only `alloc`) and contains no `unsafe`, so the same code can
//! be linked into the kernel in Phase 1 and targeted by proofs (Verus/Kani) in
//! Phase 4. In Phase 0 the host-side supervisor (`warden-sentry`) uses it to
//! hold the grants of a sandboxed Linux process.
//!
//! Invariants this module maintains (and that the tests check):
//! 1. A slot index that is empty or out of range names nothing (no forging).
//! 2. `mint` can only keep or drop rights, never add them (attenuation only).
//! 3. `revoke` removes every capability derived from the revoked one.
//! 4. Kernel memory is paid for from an untyped budget; it cannot be exceeded.
#![no_std]
#![forbid(unsafe_code)]

extern crate alloc;

pub mod cells;

use alloc::vec::Vec;

/// Rights carried by a capability. A plain bit set, kept tiny on purpose.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub struct Rights(pub u8);

impl Rights {
    pub const NONE: Rights = Rights(0);
    pub const READ: Rights = Rights(1 << 0);
    pub const WRITE: Rights = Rights(1 << 1);
    pub const EXEC: Rights = Rights(1 << 2);
    pub const MAP: Rights = Rights(1 << 3);
    /// May copy (mint) this capability to another slot or CSpace.
    pub const GRANT: Rights = Rights(1 << 4);
    pub const ALL: Rights = Rights(0b1_1111);

    pub const fn bits(self) -> u8 {
        self.0
    }
    pub const fn union(self, o: Rights) -> Rights {
        Rights(self.0 | o.0)
    }
    pub const fn intersect(self, o: Rights) -> Rights {
        Rights(self.0 & o.0)
    }
    pub const fn contains(self, o: Rights) -> bool {
        self.0 & o.0 == o.0
    }
}

/// What a capability points at. Phase 0 objects are descriptive; the kernel
/// will back each kind with a real object in Phase 1.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ObjectKind {
    Untyped,
    AddressSpace,
    Frame,
    Thread,
    Endpoint,
    Notification,
    SchedContext,
    /// A directory scope (host prototype: a granted path root).
    Dir,
    /// A network endpoint grant (host prototype: host:port pattern).
    NetEndpoint,
    /// A library approval: the right to map one hashed library image.
    Library,
    /// Machine control: stop or reset the machine. Minted for nobody by default.
    Host,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ObjectId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CapError {
    /// Slot index out of range or empty: the name refers to nothing.
    InvalidSlot,
    /// Destination slot already holds a capability.
    SlotOccupied,
    /// The capability lacks a right the operation needs.
    InsufficientRights,
    /// The capability was revoked.
    Revoked,
    /// Untyped budget exhausted.
    OutOfBudget,
    /// The object is not of the kind the operation expects.
    WrongKind,
}

#[derive(Clone, Debug)]
struct CapNode {
    object: ObjectId,
    kind: ObjectKind,
    rights: Rights,
    badge: u64,
    parent: Option<usize>,
    alive: bool,
    /// Remaining bytes, only meaningful for Untyped.
    budget: u64,
}

/// The global derivation tree. In the kernel this is the "mapping database".
#[derive(Default)]
pub struct CapTable {
    nodes: Vec<CapNode>,
    next_object: u32,
}

/// One thread's capability space. The thread can only name slots by index;
/// it never sees the table entries themselves.
#[derive(Clone, Debug)]
pub struct CSpace {
    slots: Vec<Option<usize>>,
}

/// What a successful invocation returns: the object, its kind and badge.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Resolved {
    pub object: ObjectId,
    pub kind: ObjectKind,
    pub rights: Rights,
    pub badge: u64,
}

impl CSpace {
    pub fn new(size: usize) -> Self {
        let mut slots = Vec::with_capacity(size);
        slots.resize(size, None);
        CSpace { slots }
    }
    pub fn len(&self) -> usize {
        self.slots.len()
    }
    pub fn is_empty(&self) -> bool {
        self.slots.iter().all(|s| s.is_none())
    }
    fn get(&self, slot: usize) -> Result<usize, CapError> {
        self.slots.get(slot).copied().flatten().ok_or(CapError::InvalidSlot)
    }
    fn check_free(&self, slot: usize) -> Result<(), CapError> {
        match self.slots.get(slot) {
            None => Err(CapError::InvalidSlot),
            Some(Some(_)) => Err(CapError::SlotOccupied),
            Some(None) => Ok(()),
        }
    }
    fn put(&mut self, slot: usize, idx: usize) -> Result<(), CapError> {
        match self.slots.get_mut(slot) {
            None => Err(CapError::InvalidSlot),
            Some(Some(_)) => Err(CapError::SlotOccupied),
            Some(s) => {
                *s = Some(idx);
                Ok(())
            }
        }
    }
}

impl CapTable {
    pub fn new() -> Self {
        Self::default()
    }

    fn alloc_object(&mut self) -> ObjectId {
        let id = ObjectId(self.next_object);
        self.next_object += 1;
        id
    }

    fn push(&mut self, n: CapNode) -> usize {
        self.nodes.push(n);
        self.nodes.len() - 1
    }

    fn live(&self, idx: usize) -> Result<&CapNode, CapError> {
        let n = self.nodes.get(idx).ok_or(CapError::InvalidSlot)?;
        if n.alive { Ok(n) } else { Err(CapError::Revoked) }
    }

    /// Boot-time only: create a root capability. Only the runtime that builds
    /// a container's initial CSpace from its manifest calls this.
    pub fn create_root(
        &mut self,
        cs: &mut CSpace,
        slot: usize,
        kind: ObjectKind,
        rights: Rights,
        budget: u64,
    ) -> Result<ObjectId, CapError> {
        // Check the destination before allocating: a refused request must
        // not consume kernel memory.
        cs.check_free(slot)?;
        let object = self.alloc_object();
        let idx = self.push(CapNode { object, kind, rights, badge: 0, parent: None, alive: true, budget });
        cs.put(slot, idx).expect("slot checked free");
        Ok(object)
    }

    /// Check that `slot` names a live capability with at least `need` rights.
    pub fn invoke(&self, cs: &CSpace, slot: usize, need: Rights) -> Result<Resolved, CapError> {
        let n = self.live(cs.get(slot)?)?;
        // A capability is dead if any ancestor was revoked; revoke() marks the
        // whole subtree, so checking this node suffices.
        if !n.rights.contains(need) {
            return Err(CapError::InsufficientRights);
        }
        Ok(Resolved { object: n.object, kind: n.kind, rights: n.rights, badge: n.badge })
    }

    /// Copy a capability with the same or fewer rights. The source needs GRANT.
    /// `mask` is intersected with the source rights, so it can never widen them.
    pub fn mint(
        &mut self,
        src_cs: &CSpace,
        src_slot: usize,
        dst_cs: &mut CSpace,
        dst_slot: usize,
        mask: Rights,
        badge: u64,
    ) -> Result<(), CapError> {
        let src_idx = src_cs.get(src_slot)?;
        let src = self.live(src_idx)?.clone();
        if !src.rights.contains(Rights::GRANT) {
            return Err(CapError::InsufficientRights);
        }
        // Every check happens before the allocation below, so a rejected
        // mint leaves the table exactly as it was.
        dst_cs.check_free(dst_slot)?;
        let node = CapNode {
            object: src.object,
            kind: src.kind,
            rights: src.rights.intersect(mask),
            badge: if badge != 0 { badge } else { src.badge },
            parent: Some(src_idx),
            alive: true,
            budget: 0,
        };
        let idx = self.push(node);
        dst_cs.put(dst_slot, idx).expect("slot checked free");
        Ok(())
    }

    /// Mint within one CSpace (a thread copying its own capability). Same
    /// rules as `mint`; exists so the kernel need not clone the CSpace.
    pub fn mint_self(&mut self, cs: &mut CSpace, src_slot: usize, dst_slot: usize, mask: Rights, badge: u64) -> Result<(), CapError> {
        let src_idx = cs.get(src_slot)?;
        let src = self.live(src_idx)?.clone();
        if !src.rights.contains(Rights::GRANT) {
            return Err(CapError::InsufficientRights);
        }
        cs.check_free(dst_slot)?;
        let idx = self.push(CapNode {
            object: src.object,
            kind: src.kind,
            rights: src.rights.intersect(mask),
            badge: if badge != 0 { badge } else { src.badge },
            parent: Some(src_idx),
            alive: true,
            budget: 0,
        });
        cs.put(dst_slot, idx).expect("slot checked free");
        Ok(())
    }

    /// Number of nodes ever allocated (live or revoked). Tests use it to
    /// prove that refused operations allocate nothing.
    pub fn len(&self) -> usize {
        self.nodes.len()
    }
    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }

    /// Revoke every capability derived from the one in `slot` (not the
    /// capability itself). Returns how many were revoked.
    ///
    /// Allocates nothing: the kernel heap never reclaims, so a loop of
    /// revocations must not be able to grow it. Nodes are appended in
    /// derivation order, so a child always has a larger index than its
    /// parent, and one forward pass finds the subtree. A node whose parent is
    /// dead is itself dead (mint refuses dead parents, revoke marks whole
    /// subtrees), so `alive` alone tells a killed parent from a live one.
    pub fn revoke(&mut self, cs: &CSpace, slot: usize) -> Result<usize, CapError> {
        let root = cs.get(slot)?;
        self.live(root)?;
        let mut count = 0;
        for i in (root + 1)..self.nodes.len() {
            let Some(p) = self.nodes[i].parent else { continue };
            if self.nodes[i].alive && (p == root || !self.nodes[p].alive) {
                self.nodes[i].alive = false;
                count += 1;
            }
        }
        Ok(count)
    }

    /// Pay for `bytes` of kernel memory from an untyped capability.
    pub fn retype(&mut self, cs: &CSpace, slot: usize, bytes: u64) -> Result<u64, CapError> {
        let idx = cs.get(slot)?;
        let n = self.live(idx)?;
        if n.kind != ObjectKind::Untyped {
            return Err(CapError::WrongKind);
        }
        if !n.rights.contains(Rights::WRITE) {
            return Err(CapError::InsufficientRights);
        }
        let n = &mut self.nodes[idx];
        n.budget = n.budget.checked_sub(bytes).ok_or(CapError::OutOfBudget)?;
        Ok(n.budget)
    }
}

#[cfg(test)]
mod tests {

    use super::*;

    fn setup() -> (CapTable, CSpace) {
        let mut t = CapTable::new();
        let mut cs = CSpace::new(8);
        t.create_root(&mut cs, 0, ObjectKind::Dir, Rights::READ.union(Rights::WRITE).union(Rights::GRANT), 0)
            .unwrap();
        t.create_root(&mut cs, 1, ObjectKind::Untyped, Rights::ALL, 32 << 20).unwrap();
        (t, cs)
    }

    #[test]
    fn forged_slots_name_nothing() {
        let (t, cs) = setup();
        for slot in [2usize, 7, 8, 1000, usize::MAX] {
            assert_eq!(t.invoke(&cs, slot, Rights::NONE), Err(CapError::InvalidSlot));
        }
    }

    #[test]
    fn mint_only_attenuates() {
        let (mut t, cs) = setup();
        let mut child = CSpace::new(4);
        // Ask for everything; get at most what the parent has.
        t.mint(&cs, 0, &mut child, 0, Rights::ALL, 7).unwrap();
        let r = t.invoke(&child, 0, Rights::NONE).unwrap();
        assert!(!r.rights.contains(Rights::EXEC));
        assert_eq!(r.badge, 7);
        // Read-only copy can't be used to write, and can't be re-granted.
        t.mint(&cs, 0, &mut child, 1, Rights::READ, 0).unwrap();
        assert_eq!(t.invoke(&child, 1, Rights::WRITE), Err(CapError::InsufficientRights));
        let mut grandchild = CSpace::new(1);
        assert_eq!(t.mint(&child, 1, &mut grandchild, 0, Rights::ALL, 0), Err(CapError::InsufficientRights));
    }

    #[test]
    fn exhaustive_attenuation() {
        // For every parent rights set and every mask, minted rights ⊆ parent.
        for p in 0..=Rights::ALL.bits() {
            for m in 0..=Rights::ALL.bits() {
                let mut t = CapTable::new();
                let mut a = CSpace::new(1);
                let mut b = CSpace::new(1);
                let pr = Rights(p).union(Rights::GRANT);
                t.create_root(&mut a, 0, ObjectKind::Endpoint, pr, 0).unwrap();
                t.mint(&a, 0, &mut b, 0, Rights(m), 0).unwrap();
                let got = t.invoke(&b, 0, Rights::NONE).unwrap().rights;
                assert!(pr.contains(got));
                assert!(Rights(m).contains(got));
            }
        }
    }

    #[test]
    fn revoke_kills_whole_subtree() {
        let (mut t, cs) = setup();
        let mut c1 = CSpace::new(2);
        let mut c2 = CSpace::new(2);
        t.mint(&cs, 0, &mut c1, 0, Rights::ALL, 0).unwrap();
        t.mint(&c1, 0, &mut c2, 0, Rights::ALL, 0).unwrap();
        let mut c2b = CSpace::new(2);
        t.mint(&c2, 0, &mut c2b, 1, Rights::READ, 0).unwrap();
        assert_eq!(t.revoke(&cs, 0).unwrap(), 3);
        assert_eq!(t.invoke(&c1, 0, Rights::NONE), Err(CapError::Revoked));
        assert_eq!(t.invoke(&c2b, 1, Rights::NONE), Err(CapError::Revoked));
        // The root itself survives revoke of its children.
        assert!(t.invoke(&cs, 0, Rights::READ).is_ok());
        // Revoked caps can't be minted from.
        let mut c3 = CSpace::new(1);
        assert_eq!(t.mint(&c1, 0, &mut c3, 0, Rights::ALL, 0), Err(CapError::Revoked));
    }

    #[test]
    fn untyped_budget_is_hard() {
        let (mut t, cs) = setup();
        assert_eq!(t.retype(&cs, 1, 16 << 20).unwrap(), 16 << 20);
        assert_eq!(t.retype(&cs, 1, (16 << 20) + 1), Err(CapError::OutOfBudget));
        assert_eq!(t.retype(&cs, 1, 16 << 20).unwrap(), 0);
        assert_eq!(t.retype(&cs, 0, 1), Err(CapError::WrongKind));
    }

    #[test]
    fn occupied_slot_rejected() {
        let (mut t, mut cs) = setup();
        let snapshot = cs.clone();
        assert_eq!(t.mint(&snapshot, 0, &mut cs, 1, Rights::READ, 0), Err(CapError::SlotOccupied));
    }

    #[test]
    fn mint_self_matches_mint() {
        let (mut t, mut cs) = setup();
        t.mint_self(&mut cs, 0, 2, Rights::READ, 9).unwrap();
        let r = t.invoke(&cs, 2, Rights::READ).unwrap();
        assert_eq!((r.rights, r.badge), (Rights::READ, 9));
        assert_eq!(t.mint_self(&mut cs, 0, 2, Rights::READ, 0), Err(CapError::SlotOccupied));
        assert_eq!(t.mint_self(&mut cs, 2, 3, Rights::READ, 0), Err(CapError::InsufficientRights));
        assert_eq!(t.revoke(&cs, 0).unwrap(), 1);
    }

    #[test]
    fn refused_requests_allocate_nothing() {
        let (mut t, mut cs) = setup();
        let before = t.len();
        let snapshot = cs.clone();
        for _ in 0..10_000 {
            // occupied destination, out-of-range destination, source without
            // GRANT, empty source, out-of-range root slot
            assert_eq!(t.mint(&snapshot, 0, &mut cs, 1, Rights::ALL, 0), Err(CapError::SlotOccupied));
            assert_eq!(t.mint(&snapshot, 0, &mut cs, 99, Rights::ALL, 0), Err(CapError::InvalidSlot));
            assert!(t.mint(&snapshot, 1, &mut cs, 2, Rights::ALL, 0).is_ok());
            assert_eq!(t.revoke(&cs, 1).unwrap(), 1);
            cs = snapshot.clone();
            assert_eq!(t.mint(&snapshot, 5, &mut cs, 2, Rights::ALL, 0), Err(CapError::InvalidSlot));
            assert_eq!(t.create_root(&mut cs, 0, ObjectKind::Host, Rights::ALL, 0), Err(CapError::SlotOccupied));
            assert_eq!(t.create_root(&mut cs, 1000, ObjectKind::Host, Rights::ALL, 0), Err(CapError::InvalidSlot));
        }
        // Only the 10,000 legitimate mints allocated.
        assert_eq!(t.len(), before + 10_000);
    }

    #[test]
    fn repeated_revoke_changes_nothing() {
        let (mut t, cs) = setup();
        // A real subtree so the pass has work to do: three generations.
        let mut child = CSpace::new(4);
        t.mint(&cs, 0, &mut child, 0, Rights::ALL, 1).unwrap();
        let mut grandchild = CSpace::new(4);
        t.mint(&child, 0, &mut grandchild, 0, Rights::ALL, 2).unwrap();
        let mut dir = CSpace::new(4);
        t.mint(&cs, 0, &mut dir, 0, Rights::READ, 3).unwrap();
        assert_eq!(t.revoke(&cs, 0), Ok(3));
        assert_eq!(t.invoke(&grandchild, 0, Rights::NONE), Err(CapError::Revoked));
        let nodes = t.len();
        for _ in 0..10_000 {
            // Already-revoked subtree: nothing left to kill, nothing to allocate.
            assert_eq!(t.revoke(&cs, 0), Ok(0));
            // Revoking a slot with no children, and revoking through a dead
            // capability, must also stay allocation-free.
            assert_eq!(t.revoke(&cs, 1), Ok(0));
            assert_eq!(t.revoke(&child, 0), Err(CapError::Revoked));
        }
        assert_eq!(t.len(), nodes);
    }
}
