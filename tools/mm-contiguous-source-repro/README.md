# Warden FrameAlloc source-backed regression harness

Tracks #160 and claim #161. Only `tools/mm-contiguous-source-repro/**` plus
a dedicated workflow are owned. The native memory manager is **not edited**.

`python3 tools/mm-contiguous-source-repro/run.py` extracts **the current
`FrameAlloc` source** from `crates/warden-kernel/src/mm.rs`, compiles it
using Rust `std` on an ordinary Linux host with a page-aligned backing
allocation, and exercises actual `alloc_contiguous` code. Tests:
ordinary contiguous allocation, `n=0` side effects, and a fragmented
free-list where a failed contiguous allocation may leak `used` frames.

The default is a **diagnostic** that exits 0 when the probe executes, even
if it reports a reproducible defect. Run `--strict` to fail whenever those
defects remain; enable strict mode in required CI only after the kernel owner
fixes the allocator and has run the full native boot/escape tests.

```
python3 -m unittest discover -s tools/mm-contiguous-source-repro -p 'test_*.py' -v
python3 tools/mm-contiguous-source-repro/run.py
python3 tools/mm-contiguous-source-repro/run.py --strict
```

This is an **extracted-source host reproduction**, not actual QEMU execution,
safe live kernel patching, a proof of a reachable game guest exploit, or a
measurement of whole-guest peak physical RAM. The host aligned memory stands
in for the kernel's identity-mapped physical frame pool. `high_water()`
measures bump-pointer advance and should not be confused with peak
simultaneously resident/allocated frames.

Kernel/loader owner: reject zero-length allocations without side effects,
make contiguous allocation transactional on failure, test free-list
fragmentation, and run kernel and QEMU security regressions. Independent
security review is required before merging any **kernel change**.

## Candidate patch experiment

`proposed_fix.py` contains an **unmerged** implementation of
`alloc_contiguous` that rejects zero-length requests and reserves a fresh
contiguous bump-pointer range atomically. Its arithmetic and reserved boot
module overlap are validated *before* changing `used` or `next`. It
deliberately does not consume fragmented free-list frames. This is a safety
and simplicity tradeoff, **not necessarily the best allocator for a 512 MiB
production system**, because it can reject a large contiguous request even
while enough individually free frames exist.

```
python3 tools/mm-contiguous-source-repro/run.py --proposed-fix --strict
```

The harness now also exercises allocation requests that span a reserved boot
module and requests exceeding the available frame extent. The accepted
production repair must pass those cases, preserve mappings, be reviewed by
the kernel owner, and receive real QEMU and regression-test evidence.
