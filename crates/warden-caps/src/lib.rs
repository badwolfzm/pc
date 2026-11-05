//! Warden capability model.
//!
//! This crate is the object model the Warden microkernel enforces. It is
//! `no_std` (needs only `alloc`) and contains no `unsafe`, so the same code can
//! be linked into the kernel and targeted by proofs (Verus/Kani) in Phase 4.
//! In Phase 0 the host-side supervisor (`warden-sentry`) uses it to hold the
//! grants of a sandboxed Linux process.
//!
//! Invariants this module maintains (and that the tests check):
//! 1. A slot index that is empty or out of range names nothing (no forging).
//! 2. `mint` can only keep or drop rights, never add them (attenuation only).
//! 3. `revoke` removes every capability derived from the revoked one.
//! 4. Every kernel object, and every capability node pointing at an object
//!    somebody paid for, is paid for from an untyped budget. A refused request
//!    consumes nothing. Freeing a capability refunds its node, and freeing the
//!    last capability to an object refunds the object, so the memory can be
//!    used again (Phase 3).
//!
//! Memory model. The table keeps two arenas with free lists: capability nodes
//! (the derivation tree) and objects (what nodes point at). A freed entry is
//! reused; its generation counter is bumped so a slot that still names the
//! old entry resolves to `Revoked`, never to the new occupant. Objects created
//! at boot (`create_root`) have no owner and cost nothing: the number of nodes
//! that can point at them is bounded by the number of CSpace slots, which
//! are themselves kernel memory the kernel sizes at thread creation.
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

/// What a capability points at.
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

impl ObjectKind {
    /// Kernel kinds a thread may create from untyped memory with `retype`,
    /// encoded for the syscall boundary. Anything else is not retypable.
    pub fn from_retype_code(code: u64) -> Option<ObjectKind> {
        Some(match code {
            0 => ObjectKind::Untyped,
            1 => ObjectKind::Frame,
            2 => ObjectKind::Endpoint,
            3 => ObjectKind::Notification,
            _ => return None,
        })
    }

    /// Bytes of kernel memory an object of this kind costs, excluding the
    /// capability node that names it. `Untyped` is sized by the caller.
    /// Kinds that cannot be created by retype cost 0 (they only exist as
    /// boot-time roots).
    pub const fn bytes(self) -> u64 {
        match self {
            ObjectKind::Frame => 4096,
            ObjectKind::Endpoint => 256,
            ObjectKind::Notification => 64,
            ObjectKind::AddressSpace => 4 * 4096,
            ObjectKind::Thread => 8192,
            ObjectKind::SchedContext => 64,
            ObjectKind::Untyped | ObjectKind::Dir | ObjectKind::NetEndpoint | ObjectKind::Library | ObjectKind::Host => 0,
        }
    }
}

/// Bytes charged for one capability node (one entry of the derivation tree).
pub const NODE_BYTES: u64 = 64;

/// Smallest untyped a split may produce; stops a thread making millions of
/// one-byte children (each child is an object plus a node, both charged).
pub const MIN_UNTYPED: u64 = 64;

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
    /// The capability was revoked (or its object was freed).
    Revoked,
    /// Untyped budget exhausted.
    OutOfBudget,
    /// The object is not of the kind the operation expects.
    WrongKind,
}

#[derive(Clone, Debug)]
struct CapNode {
    object: usize,
    rights: Rights,
    badge: u64,
    parent: Option<usize>,
    /// Generation of the parent node when this node was derived; a parent
    /// slot reused since then is not this node's parent any more.
    parent_gen: u32,
    alive: bool,
    gen: u32,
    /// Scratch bit for `kill_subtree`; always false between operations.
    mark: bool,
    /// Free-list link while the entry is unused (`NONE` ends the list).
    next_free: usize,
}

#[derive(Clone, Debug)]
struct Object {
    id: ObjectId,
    kind: ObjectKind,
    /// Untyped object that paid for this object, or None for boot objects.
    owner: Option<usize>,
    /// Bytes charged to the owner for the object itself.
    bytes: u64,
    /// Live nodes pointing at this object.
    refs: u32,
    /// Untyped only: bytes not yet handed out.
    free: u64,
    alive: bool,
    /// Link while on the pending-reclaim list or the free list.
    next_free: usize,
}

/// End of an intrusive list.
const NONE: usize = usize::MAX;

/// The global derivation tree plus the object table. In the kernel this is
/// the "mapping database".
/// The free lists are threaded through the unused entries themselves, so
/// freeing never allocates: a flood of create/delete cannot grow the heap.
pub struct CapTable {
    nodes: Vec<CapNode>,
    /// Head of the free node list.
    free_nodes: usize,
    objects: Vec<Object>,
    /// Head of the free object list (drained, reusable).
    free_objects: usize,
    next_id: u32,
    /// Head of the list of objects freed since the kernel last drained; it
    /// releases their backing state (frames, queues), then they become free.
    freed: usize,
    /// Nodes freed since the table was created (for counting one operation).
    freed_nodes: usize,
}

impl Default for CapTable {
    fn default() -> Self {
        CapTable { nodes: Vec::new(), free_nodes: NONE, objects: Vec::new(), free_objects: NONE, next_id: 0, freed: NONE, freed_nodes: 0 }
    }
}

/// A slot's content: node index plus the node generation it was issued for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Slot {
    idx: usize,
    gen: u32,
}

/// One thread's capability space. The thread can only name slots by index;
/// it never sees the table entries themselves.
#[derive(Clone, Debug)]
pub struct CSpace {
    slots: Vec<Option<Slot>>,
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
    fn raw(&self, slot: usize) -> Result<Slot, CapError> {
        self.slots.get(slot).copied().flatten().ok_or(CapError::InvalidSlot)
    }
    fn put(&mut self, slot: usize, s: Slot) {
        self.slots[slot] = Some(s);
    }
    fn clear(&mut self, slot: usize) {
        self.slots[slot] = None;
    }
}

impl CapTable {
    pub fn new() -> Self {
        Self::default()
    }

    /// A table whose arenas and free lists are sized up front, so that
    /// creating and freeing up to `nodes` capabilities and `objects` objects
    /// never touches the heap. The kernel uses this so its heap use after
    /// boot is a constant, not a function of what threads do.
    pub fn with_capacity(nodes: usize, objects: usize) -> Self {
        CapTable { nodes: Vec::with_capacity(nodes), objects: Vec::with_capacity(objects), ..Self::default() }
    }

    // ---- node and object arenas -------------------------------------------

    fn node_live(&self, s: Slot) -> Result<&CapNode, CapError> {
        let n = self.nodes.get(s.idx).ok_or(CapError::InvalidSlot)?;
        if n.alive && n.gen == s.gen { Ok(n) } else { Err(CapError::Revoked) }
    }

    /// Resolve a slot to a live node index.
    fn get(&self, cs: &CSpace, slot: usize) -> Result<usize, CapError> {
        let s = cs.raw(slot)?;
        self.node_live(s)?;
        Ok(s.idx)
    }

    /// A destination slot must exist and hold nothing live. A slot that still
    /// names a revoked node counts as free: it names nothing.
    fn check_free(&self, cs: &CSpace, slot: usize) -> Result<(), CapError> {
        match cs.slots.get(slot) {
            None => Err(CapError::InvalidSlot),
            Some(None) => Ok(()),
            Some(Some(s)) => match self.node_live(*s) {
                Ok(_) => Err(CapError::SlotOccupied),
                Err(_) => Ok(()),
            },
        }
    }

    fn alloc_node(&mut self, mut n: CapNode) -> Slot {
        n.alive = true;
        n.next_free = NONE;
        if self.free_nodes != NONE {
            let idx = self.free_nodes;
            self.free_nodes = self.nodes[idx].next_free;
            // The generation was bumped when the node was freed.
            n.gen = self.nodes[idx].gen;
            self.nodes[idx] = n;
            Slot { idx, gen: self.nodes[idx].gen }
        } else {
            n.gen = 0;
            self.nodes.push(n);
            Slot { idx: self.nodes.len() - 1, gen: 0 }
        }
    }

    fn alloc_object(&mut self, kind: ObjectKind, owner: Option<usize>, bytes: u64, total: u64) -> usize {
        let id = ObjectId(self.next_id);
        self.next_id += 1;
        let o = Object { id, kind, owner, bytes, refs: 0, free: total, alive: true, next_free: NONE };
        if self.free_objects != NONE {
            let i = self.free_objects;
            self.free_objects = self.objects[i].next_free;
            self.objects[i] = o;
            i
        } else {
            self.objects.push(o);
            self.objects.len() - 1
        }
    }

    /// Release one node: refund it, drop its object reference, free the
    /// object when that was the last reference. Nodes derived from it are
    /// re-parented to its parent, so a later revoke higher up still reaches
    /// them. Freeing a node twice is a no-op.
    fn free_node(&mut self, idx: usize) {
        if !self.nodes[idx].alive {
            self.nodes[idx].mark = false;
            return;
        }
        let (old_gen, obj, parent, parent_gen) = {
            let n = &mut self.nodes[idx];
            n.alive = false;
            n.mark = false;
            let g = n.gen;
            n.gen = n.gen.wrapping_add(1);
            (g, n.object, n.parent, n.parent_gen)
        };
        for i in 0..self.nodes.len() {
            let c = &mut self.nodes[i];
            if c.alive && c.parent == Some(idx) && c.parent_gen == old_gen {
                c.parent = parent;
                c.parent_gen = parent_gen;
            }
        }
        self.nodes[idx].next_free = self.free_nodes;
        self.free_nodes = idx;
        self.freed_nodes += 1;
        if let Some(owner) = self.objects[obj].owner {
            self.objects[owner].free += NODE_BYTES;
        }
        let o = &mut self.objects[obj];
        o.refs -= 1;
        if o.refs == 0 {
            self.free_object(obj);
        }
    }

    /// Free an object whose last capability went away. An untyped takes with
    /// it every object it paid for (their memory came from it), then refunds
    /// its whole size to its own owner.
    fn free_object(&mut self, i: usize) {
        if !self.objects[i].alive {
            return;
        }
        if self.objects[i].kind == ObjectKind::Untyped {
            loop {
                let victim = (0..self.nodes.len()).find(|&n| {
                    let node = &self.nodes[n];
                    node.alive && self.objects[node.object].owner == Some(i)
                });
                match victim {
                    Some(n) => {
                        self.kill_subtree(n, true);
                    }
                    None => break,
                }
            }
        }
        let (owner, bytes) = {
            let o = &mut self.objects[i];
            o.alive = false;
            o.next_free = self.freed;
            (o.owner, o.bytes)
        };
        self.freed = i;
        if let Some(owner) = owner {
            self.objects[owner].free += bytes;
        }
    }

    /// Next object freed since the last call, so the kernel can release its
    /// backing state. The entry becomes reusable only after it is taken, so
    /// the id and kind returned are the freed object's. Returns None when
    /// nothing is pending.
    pub fn take_freed(&mut self) -> Option<(ObjectId, ObjectKind)> {
        if self.freed == NONE {
            return None;
        }
        let i = self.freed;
        let o = &mut self.objects[i];
        self.freed = o.next_free;
        o.next_free = self.free_objects;
        self.free_objects = i;
        Some((o.id, o.kind))
    }

    /// Kill every live node derived (transitively) from node `root`, then
    /// optionally the root itself. Allocates nothing: the subtree is found
    /// with a mark bit in the nodes themselves. Children may have smaller
    /// indices than their parents once entries are reused, so this iterates
    /// to a fixpoint; each pass marks at least one whole level. Returns the
    /// number of nodes freed.
    fn kill_subtree(&mut self, root: usize, include_root: bool) -> usize {
        self.nodes[root].mark = true;
        loop {
            let mut progress = false;
            for i in 0..self.nodes.len() {
                if self.nodes[i].mark || !self.nodes[i].alive {
                    continue;
                }
                if let Some(p) = self.nodes[i].parent {
                    if self.nodes[p].mark && self.nodes[i].parent_gen == self.nodes[p].gen {
                        self.nodes[i].mark = true;
                        progress = true;
                    }
                }
            }
            if !progress {
                break;
            }
        }
        // Freeing one node may free others (an untyped's last capability
        // takes its objects along), so count what the table actually freed.
        // free_node clears the mark.
        let before = self.freed_nodes;
        for i in (0..self.nodes.len()).rev() {
            if i != root && self.nodes[i].mark {
                self.free_node(i);
            }
        }
        if include_root {
            self.free_node(root);
        }
        self.nodes[root].mark = false;
        self.freed_nodes - before
    }

    // ---- public API ---------------------------------------------------------

    /// Boot-time only: create a root capability to a fresh, unowned object.
    /// Only the runtime that builds a container's initial CSpace calls this.
    /// For `Untyped`, `budget` is the bytes the object can hand out.
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
        self.check_free(cs, slot)?;
        let total = if kind == ObjectKind::Untyped { budget } else { 0 };
        let obj = self.alloc_object(kind, None, 0, total);
        self.objects[obj].refs = 1;
        let s = self.alloc_node(CapNode { object: obj, rights, badge: 0, parent: None, parent_gen: 0, alive: true, gen: 0, mark: false, next_free: NONE });
        cs.put(slot, s);
        Ok(self.objects[obj].id)
    }

    /// Check that `slot` names a live capability with at least `need` rights.
    pub fn invoke(&self, cs: &CSpace, slot: usize, need: Rights) -> Result<Resolved, CapError> {
        let n = self.node_live(cs.raw(slot)?)?;
        if !n.rights.contains(need) {
            return Err(CapError::InsufficientRights);
        }
        let o = &self.objects[n.object];
        Ok(Resolved { object: o.id, kind: o.kind, rights: n.rights, badge: n.badge })
    }

    /// Can the owner of this object's budget pay for `bytes` more?
    fn charge(&mut self, owner: Option<usize>, bytes: u64) -> Result<(), CapError> {
        if let Some(owner) = owner {
            let o = &mut self.objects[owner];
            o.free = o.free.checked_sub(bytes).ok_or(CapError::OutOfBudget)?;
        }
        Ok(())
    }

    fn derive(&mut self, src_idx: usize, mask: Rights, badge: u64) -> Result<CapNode, CapError> {
        let src = self.nodes[src_idx].clone();
        if !src.rights.contains(Rights::GRANT) {
            return Err(CapError::InsufficientRights);
        }
        Ok(CapNode {
            object: src.object,
            rights: src.rights.intersect(mask),
            badge: if badge != 0 { badge } else { src.badge },
            parent: Some(src_idx),
            parent_gen: src.gen,
            alive: true,
            gen: 0,
            mark: false,
            next_free: NONE,
        })
    }

    /// Copy a capability with the same or fewer rights. The source needs GRANT.
    /// `mask` is intersected with the source rights, so it can never widen them.
    /// The new node is charged to the untyped that paid for the object.
    pub fn mint(
        &mut self,
        src_cs: &CSpace,
        src_slot: usize,
        dst_cs: &mut CSpace,
        dst_slot: usize,
        mask: Rights,
        badge: u64,
    ) -> Result<(), CapError> {
        let src_idx = self.get(src_cs, src_slot)?;
        let node = self.derive(src_idx, mask, badge)?;
        // Every check happens before the allocation below, so a rejected
        // mint leaves the table exactly as it was.
        self.check_free(dst_cs, dst_slot)?;
        let owner = self.objects[node.object].owner;
        self.charge(owner, NODE_BYTES)?;
        self.objects[node.object].refs += 1;
        let s = self.alloc_node(node);
        dst_cs.put(dst_slot, s);
        Ok(())
    }

    /// Mint within one CSpace (a thread copying its own capability). Same
    /// rules as `mint`; exists so the kernel need not clone the CSpace.
    pub fn mint_self(&mut self, cs: &mut CSpace, src_slot: usize, dst_slot: usize, mask: Rights, badge: u64) -> Result<(), CapError> {
        let src_idx = self.get(cs, src_slot)?;
        let node = self.derive(src_idx, mask, badge)?;
        self.check_free(cs, dst_slot)?;
        let owner = self.objects[node.object].owner;
        self.charge(owner, NODE_BYTES)?;
        self.objects[node.object].refs += 1;
        let s = self.alloc_node(node);
        cs.put(dst_slot, s);
        Ok(())
    }

    /// Create a kernel object of `kind`, paid for from the untyped in
    /// `untyped_slot` (needs WRITE), and place a full-rights root capability
    /// for it in `dst_slot`. For `Untyped`, `size` is the child's budget
    /// (at least `MIN_UNTYPED`); for other kinds the size is fixed by the
    /// kind. Costs `kind.bytes() + NODE_BYTES`. Returns the object and the
    /// untyped's remaining budget. A refused retype consumes nothing.
    pub fn retype(
        &mut self,
        cs: &mut CSpace,
        untyped_slot: usize,
        kind: ObjectKind,
        size: u64,
        dst_slot: usize,
    ) -> Result<(ObjectId, u64), CapError> {
        let uidx = self.get(cs, untyped_slot)?;
        let unode = &self.nodes[uidx];
        let uobj = unode.object;
        if self.objects[uobj].kind != ObjectKind::Untyped {
            return Err(CapError::WrongKind);
        }
        if !unode.rights.contains(Rights::WRITE) {
            return Err(CapError::InsufficientRights);
        }
        let bytes = match kind {
            ObjectKind::Untyped => {
                if size < MIN_UNTYPED {
                    return Err(CapError::WrongKind);
                }
                size
            }
            ObjectKind::Frame | ObjectKind::Endpoint | ObjectKind::Notification => kind.bytes(),
            _ => return Err(CapError::WrongKind),
        };
        self.check_free(cs, dst_slot)?;
        let total = bytes.checked_add(NODE_BYTES).ok_or(CapError::OutOfBudget)?;
        self.charge(Some(uobj), total)?;
        let total_budget = if kind == ObjectKind::Untyped { size } else { 0 };
        let obj = self.alloc_object(kind, Some(uobj), bytes, total_budget);
        self.objects[obj].refs = 1;
        // The new root is a child of the untyped's node in the derivation
        // tree: revoking or deleting the untyped takes the object with it.
        let ugen = self.nodes[uidx].gen;
        let s = self.alloc_node(CapNode { object: obj, rights: Rights::ALL, badge: 0, parent: Some(uidx), parent_gen: ugen, alive: true, gen: 0, mark: false, next_free: NONE });
        cs.put(dst_slot, s);
        Ok((self.objects[obj].id, self.objects[uobj].free))
    }

    /// Remaining budget of the untyped in `slot` (any rights).
    pub fn budget(&self, cs: &CSpace, slot: usize) -> Result<u64, CapError> {
        let idx = self.get(cs, slot)?;
        let o = &self.objects[self.nodes[idx].object];
        if o.kind != ObjectKind::Untyped {
            return Err(CapError::WrongKind);
        }
        Ok(o.free)
    }

    /// Remove the capability in `slot`. Capabilities derived from it survive
    /// (they are re-parented, as in seL4; use `revoke` to kill them), but if
    /// this was the last capability to an object the object is freed, and a
    /// freed untyped takes every object it paid for. Refunds everything
    /// freed. Returns how many nodes were freed.
    pub fn delete(&mut self, cs: &mut CSpace, slot: usize) -> Result<usize, CapError> {
        let idx = self.get(cs, slot)?;
        let before = self.freed_nodes;
        self.free_node(idx);
        cs.clear(slot);
        Ok(self.freed_nodes - before)
    }

    /// Number of node entries ever allocated (live or free). Tests use it to
    /// prove that refused operations allocate nothing and that freed entries
    /// are reused.
    pub fn len(&self) -> usize {
        self.nodes.len()
    }
    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }
    /// Live capability nodes.
    pub fn live_nodes(&self) -> usize {
        self.nodes.iter().filter(|n| n.alive).count()
    }
    /// Live objects.
    pub fn live_objects(&self) -> usize {
        self.objects.iter().filter(|o| o.alive).count()
    }
    /// Object entries ever allocated (live or free).
    pub fn object_slots(&self) -> usize {
        self.objects.len()
    }

    /// Revoke every capability derived from the one in `slot` (not the
    /// capability itself). Returns how many were revoked.
    pub fn revoke(&mut self, cs: &CSpace, slot: usize) -> Result<usize, CapError> {
        let root = self.get(cs, slot)?;
        Ok(self.kill_subtree(root, false))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const U_BUDGET: u64 = 32 << 20;

    fn setup() -> (CapTable, CSpace) {
        let mut t = CapTable::new();
        let mut cs = CSpace::new(8);
        t.create_root(&mut cs, 0, ObjectKind::Dir, Rights::READ.union(Rights::WRITE).union(Rights::GRANT), 0)
            .unwrap();
        t.create_root(&mut cs, 1, ObjectKind::Untyped, Rights::ALL, U_BUDGET).unwrap();
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
        let mut t = CapTable::new();
        let mut cs = CSpace::new(8);
        t.create_root(&mut cs, 0, ObjectKind::Dir, Rights::ALL, 0).unwrap();
        // Room for exactly two frames (each 4096 + a 64-byte node).
        t.create_root(&mut cs, 1, ObjectKind::Untyped, Rights::ALL, 2 * (4096 + NODE_BYTES)).unwrap();
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Frame, 0, 2).unwrap().1, 4096 + NODE_BYTES);
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Frame, 0, 3).unwrap().1, 0);
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Frame, 0, 4), Err(CapError::OutOfBudget));
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Notification, 0, 4), Err(CapError::OutOfBudget));
        assert_eq!(t.retype(&mut cs, 0, ObjectKind::Frame, 0, 4), Err(CapError::WrongKind));
        // Not retypable: Host, Thread (not yet), tiny untyped.
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Host, 0, 4), Err(CapError::WrongKind));
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Thread, 0, 4), Err(CapError::WrongKind));
        assert_eq!(t.retype(&mut cs, 1, ObjectKind::Untyped, 1, 4), Err(CapError::WrongKind));
        // A read-only untyped cannot pay. (Copying a boot root costs nothing:
        // the root object has no owner.)
        let mut ro = CSpace::new(2);
        t.mint(&cs, 1, &mut ro, 0, Rights::READ, 0).unwrap();
        assert_eq!(t.retype(&mut ro, 0, ObjectKind::Notification, 0, 1), Err(CapError::InsufficientRights));
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
        let before_objs = t.object_slots();
        let budget = t.budget(&cs, 1).unwrap();
        let snapshot = cs.clone();
        for _ in 0..10_000 {
            // occupied destination, out-of-range destination, source without
            // GRANT, empty source, out-of-range root slot, over-budget and
            // wrong-kind retypes
            assert_eq!(t.mint(&snapshot, 0, &mut cs, 1, Rights::ALL, 0), Err(CapError::SlotOccupied));
            assert_eq!(t.mint(&snapshot, 0, &mut cs, 99, Rights::ALL, 0), Err(CapError::InvalidSlot));
            assert!(t.mint(&snapshot, 1, &mut cs, 2, Rights::ALL, 0).is_ok());
            assert_eq!(t.revoke(&cs, 1).unwrap(), 1);
            cs = snapshot.clone();
            assert_eq!(t.mint(&snapshot, 5, &mut cs, 2, Rights::ALL, 0), Err(CapError::InvalidSlot));
            assert_eq!(t.create_root(&mut cs, 0, ObjectKind::Host, Rights::ALL, 0), Err(CapError::SlotOccupied));
            assert_eq!(t.create_root(&mut cs, 1000, ObjectKind::Host, Rights::ALL, 0), Err(CapError::InvalidSlot));
            assert_eq!(t.retype(&mut cs, 1, ObjectKind::Untyped, U_BUDGET + 1, 2), Err(CapError::OutOfBudget));
            assert_eq!(t.retype(&mut cs, 1, ObjectKind::Frame, 0, 0), Err(CapError::SlotOccupied));
            assert_eq!(t.retype(&mut cs, 1, ObjectKind::Frame, 0, 800), Err(CapError::InvalidSlot));
            assert_eq!(t.retype(&mut cs, 0, ObjectKind::Frame, 0, 2), Err(CapError::WrongKind));
            assert_eq!(t.delete(&mut cs, 7), Err(CapError::InvalidSlot));
        }
        // The one legitimate mint per round was revoked each time and its
        // node reused: the arena never grew past one extra entry.
        assert_eq!(t.len(), before + 1);
        assert_eq!(t.object_slots(), before_objs);
        assert_eq!(t.budget(&cs, 1).unwrap(), budget);
        assert_eq!(t.take_freed(), None);
    }

    #[test]
    fn freed_objects_are_reused_and_refunded() {
        let mut t = CapTable::new();
        let mut cs = CSpace::new(16);
        let budget = 3 * (4096 + NODE_BYTES);
        t.create_root(&mut cs, 0, ObjectKind::Untyped, Rights::ALL, budget).unwrap();
        let nodes0 = t.len();
        let objs0 = t.object_slots();
        for round in 0..1_000 {
            let (f0, _) = t.retype(&mut cs, 0, ObjectKind::Frame, 0, 1).unwrap();
            let (f1, _) = t.retype(&mut cs, 0, ObjectKind::Frame, 0, 2).unwrap();
            let (f2, rem) = t.retype(&mut cs, 0, ObjectKind::Frame, 0, 3).unwrap();
            assert_eq!(rem, 0);
            assert_eq!(t.retype(&mut cs, 0, ObjectKind::Frame, 0, 4), Err(CapError::OutOfBudget));
            // Free one; the budget comes back and the fourth now fits.
            assert_eq!(t.delete(&mut cs, 2).unwrap(), 1);
            assert_eq!(t.take_freed(), Some((f1, ObjectKind::Frame)));
            assert_eq!(t.budget(&cs, 0).unwrap(), 4096 + NODE_BYTES);
            // The deleted slot is empty again; it names nothing.
            assert_eq!(t.invoke(&cs, 2, Rights::NONE), Err(CapError::InvalidSlot));
            let (f3, rem) = t.retype(&mut cs, 0, ObjectKind::Frame, 0, 4).unwrap();
            assert_eq!(rem, 0);
            assert_ne!(f3, f1, "object ids are never reused");
            // Delete all, budget whole again.
            for s in [1, 3, 4] {
                t.delete(&mut cs, s).unwrap();
            }
            let mut freed: Vec<ObjectId> = core::iter::from_fn(|| t.take_freed()).map(|(id, _)| id).collect();
            freed.sort();
            let mut want = [f0, f2, f3];
            want.sort();
            assert_eq!(freed, want);
            assert_eq!(t.budget(&cs, 0).unwrap(), budget);
            if round == 0 {
                // Arena grew by exactly the peak (3 frames live at once).
                assert_eq!(t.len(), nodes0 + 3);
                assert_eq!(t.object_slots(), objs0 + 3);
            }
        }
        // 4,000 objects created and freed: arena still at the first round's peak.
        assert_eq!(t.len(), nodes0 + 3);
        assert_eq!(t.object_slots(), objs0 + 3);
        assert_eq!(t.live_objects(), 1);
    }

    #[test]
    fn stale_slot_never_names_a_reused_node() {
        let mut t = CapTable::new();
        let mut a = CSpace::new(4);
        let mut b = CSpace::new(4);
        t.create_root(&mut a, 0, ObjectKind::Untyped, Rights::ALL, 1 << 20).unwrap();
        t.create_root(&mut a, 1, ObjectKind::Host, Rights::ALL, 0).unwrap();
        // b gets a copy of the host cap, then it is revoked and its node reused
        // for an endpoint created by a.
        t.mint(&a, 1, &mut b, 0, Rights::ALL, 0).unwrap();
        assert_eq!(t.revoke(&a, 1).unwrap(), 1);
        t.retype(&mut a, 0, ObjectKind::Endpoint, 0, 2).unwrap();
        // b's slot 0 must not have become the endpoint.
        assert_eq!(t.invoke(&b, 0, Rights::NONE), Err(CapError::Revoked));
        // ... and b may overwrite its stale slot.
        t.mint(&a, 2, &mut b, 0, Rights::READ, 0).unwrap();
        assert_eq!(t.invoke(&b, 0, Rights::READ).unwrap().kind, ObjectKind::Endpoint);
    }

    #[test]
    fn deleting_an_untyped_frees_everything_it_paid_for() {
        let mut t = CapTable::new();
        let mut cs = CSpace::new(8);
        t.create_root(&mut cs, 0, ObjectKind::Untyped, Rights::ALL, 1 << 20).unwrap();
        // Split a child untyped, create objects from both, mint copies.
        t.retype(&mut cs, 0, ObjectKind::Untyped, 64 << 10, 1).unwrap();
        t.retype(&mut cs, 1, ObjectKind::Frame, 0, 2).unwrap();
        t.retype(&mut cs, 1, ObjectKind::Endpoint, 0, 3).unwrap();
        t.retype(&mut cs, 0, ObjectKind::Frame, 0, 4).unwrap();
        let mut other = CSpace::new(2);
        t.mint(&cs, 3, &mut other, 0, Rights::WRITE, 5).unwrap();
        assert_eq!(t.budget(&cs, 1).unwrap(), (64 << 10) - (4096 + 256 + 3 * NODE_BYTES));
        // Delete the child untyped: its frame, endpoint and the endpoint copy go.
        let n = t.delete(&mut cs, 1).unwrap();
        assert_eq!(n, 4);
        assert_eq!(t.invoke(&cs, 2, Rights::NONE), Err(CapError::Revoked));
        assert_eq!(t.invoke(&other, 0, Rights::NONE), Err(CapError::Revoked));
        assert!(t.invoke(&cs, 4, Rights::NONE).is_ok(), "sibling paid by the parent survives");
        assert_eq!(t.budget(&cs, 0).unwrap(), (1 << 20) - (4096 + NODE_BYTES));
        let mut kinds: Vec<ObjectKind> = core::iter::from_fn(|| t.take_freed()).map(|(_, k)| k).collect();
        kinds.sort_by_key(|k| *k as u8);
        assert_eq!(kinds.len(), 3);
        assert!(kinds.contains(&ObjectKind::Untyped) && kinds.contains(&ObjectKind::Frame) && kinds.contains(&ObjectKind::Endpoint));
        assert_eq!(t.live_objects(), 2);
    }

    #[test]
    fn delete_keeps_derived_copies_alive() {
        // seL4 semantics: deleting a capability is not revoking it. The copy
        // keeps the object alive; the object goes when the copy goes.
        let mut t = CapTable::new();
        let mut a = CSpace::new(4);
        let mut b = CSpace::new(4);
        t.create_root(&mut a, 0, ObjectKind::Untyped, Rights::ALL, 1 << 20).unwrap();
        let (ep, _) = t.retype(&mut a, 0, ObjectKind::Endpoint, 0, 1).unwrap();
        t.mint(&a, 1, &mut b, 0, Rights::WRITE, 3).unwrap();
        assert_eq!(t.delete(&mut a, 1).unwrap(), 1);
        assert_eq!(t.take_freed(), None);
        assert_eq!(t.invoke(&b, 0, Rights::WRITE).unwrap().object, ep);
        // The copy was re-parented to the untyped: revoking the untyped's
        // children still reaches it.
        assert_eq!(t.revoke(&a, 0).unwrap(), 1);
        assert_eq!(t.take_freed(), Some((ep, ObjectKind::Endpoint)));
        assert_eq!(t.budget(&a, 0).unwrap(), 1 << 20);
    }

    #[test]
    fn revoke_refunds_minted_nodes() {
        let mut t = CapTable::new();
        let mut cs = CSpace::new(8);
        t.create_root(&mut cs, 0, ObjectKind::Untyped, Rights::ALL, 1 << 20).unwrap();
        t.retype(&mut cs, 0, ObjectKind::Endpoint, 0, 1).unwrap();
        let after_ep = t.budget(&cs, 0).unwrap();
        for s in 2..8 {
            t.mint_self(&mut cs, 1, s, Rights::WRITE, s as u64).unwrap();
        }
        assert_eq!(t.budget(&cs, 0).unwrap(), after_ep - 6 * NODE_BYTES);
        assert_eq!(t.revoke(&cs, 1).unwrap(), 6);
        assert_eq!(t.budget(&cs, 0).unwrap(), after_ep);
        assert_eq!(t.take_freed(), None, "the endpoint itself is still held");
    }
}
