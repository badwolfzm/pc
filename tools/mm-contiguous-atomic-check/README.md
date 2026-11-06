# Proposed fix for Warden `FrameAlloc::alloc_contiguous` (#160)

This branch alters the kernel's `mm.rs` **only as a draft for the kernel owner to
review**. The original method calls `alloc()` repeatedly. If free-list frames
are nonadjacent, an early return leaks already acquired frames and increments
`used` incorrectly; even `alloc_contiguous(0)` allocates a frame.
Issue #160 and source-backed reproducer PR #163 document the defect.

The proposed minimal behavior reserves **only fresh contiguous bump memory**:
- `n=0` and arithmetic overflow fail without state changes.
- Check the entire reservation, including the reserved boot-module interval,
  before incrementing `used` and advancing `next`.
- Zero the committed contiguous span.
- No free-list frames are popped by contiguous requests. This is conservative:
  an otherwise viable contiguous run in the free list will not be used.
- Existing ordinary `alloc()` and `free()` paths remain unchanged.

Run `python3 tools/mm-contiguous-atomic-check/check.py` to extract and
compile the **actual** `FrameAlloc` Rust source under an aligned Linux-host
backing region and check zero request, overflow, fresh success, fragmented
free-list reuse preservation, skip/reserve and failure without accounting
changes. This is not a native Warden/QEMU test and does not by itself
verify full RAM accounting, memory isolation or 512 MiB game compatibility.

The existing diagnostic PR #163's harness explicitly expects a fragmented
request to FAIL, so it must be updated by its owner to permit a correct
fresh-span success before its `--strict` gate can be used with this design.

Security/kernel owner must review bump exhaustion and trade-offs, possible
user-sensitive allocation sites, `used` accounting, alignment, physical
identity mapping, new page-table/regression tests and kernel/QEMU before
merging. No merges or broad permissions are authorized by this draft.

Task: mm-contiguous-transactional-fresh-20261106. Claim issue #166.
