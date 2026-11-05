# Warden swap and frame-pressure prototype (host only)

A **bounded Python reference model**, not Warden kernel swap, not a guest process,
not a QEMU benchmark, and not a reason to enable swapping secrets to unencrypted
media. It tests the algorithm and failure cases before a kernel owner designs
page-fault integration.

```sh
python3 -m unittest discover -s tools/warden-swap-probe -p 'test_*.py' -v
python3 tools/warden-swap-probe/swap_probe.py --pages 256 --ram-pages 64 --rounds 3
```

It models 4 KiB anonymous pages, a bounded resident LRU, a bounded temporary-file
swap area, dirty-page writeback, page-in with SHA-256 validation, and explicit
out-of-budget failure. The backing file is unlinked and temporary; it is not
persistent swap or a secure encrypted store. Host Python and OS page caches
consume additional RAM **not** accounted by `resident_bytes`.

**Integration gates before kernel work:**
1. Kernel owner defines page-fault/VMA semantics; zero-fill, copy-on-write,
   memory capabilities, revocation and unmap. Never map unchecked guest pointers.
2. Choose an authorized block-device/storage service with quotas, encryption,
   integrity and cleanup on termination. Avoid writing sensitive pages to plaintext.
3. Keep executable pages and pinned framebuffer/input/IPC pages resident;
   use discard/reload for verified immutable asset pages before swapping.
4. Fault outside the rendering critical path where possible; use prefetch and
   deterministic residency budgets; record p50/p95/p99 and worst-case fault time.
5. Fail closed on full storage, I/O error and digest mismatch; test revocation,
   stale mappings, concurrent faults, cancellation, swap-slot recycling, and
   malformed requests. This prototype is single-threaded and does not cover
   these concurrent kernel cases.
6. Compare without-swap, cold-swap, hot-cache and pressure workloads using
   `-smp 1`, pinned CPU, a realistic physical-storage backend, and measured
   guest/host memory. Never interpret Python temp-file throughput as SSD speed.

**Performance policy:** one-vCPU smoothness needs low fault rate, especially
for graphics and input. Swapping is a capacity fallback, not an FPS booster.
Prioritize stable working sets, frame tile reuse, bounded asset streaming,
fast software drawing, low-res rendering and frame pacing. Albion Online's
runtime, security and graphics dependencies are not demonstrated by this model.

**Security limitation:** integrity digest is not keyed authentication; an
attacker able to edit both data and in-memory digests may bypass it. There is
no encryption, secure deletion, persistent metadata recovery, fault parallelism
or hardened file descriptor containment. Never deploy as a privileged pager.
