# 512 MiB single-vCPU game memory trace study (offline only)

Tracks the *ordinary modern PC games* research objective of PR #134. This tool replays **supplied page-reference traces** and compares FIFO, LRU and Belady's OPT (an unrealizable future-aware lower bound). It does not gather traces, implement Warden swap, run a game or measure an actual machine. It never accesses storage outside an explicitly provided CSV file; no raw device access or OS paging is performed.

Run:

```sh
python3 -m unittest discover -s tools/game-512mb-trace -p 'test_*.py' -v
python3 tools/game-512mb-trace/sim.py tools/game-512mb-trace/example-synthetic.csv --total-mib 512 --reserve-mib 192 --page-bytes 4096 --frame-meta-bytes 64 --read-us 100 --write-us 100
```

CSV must have exact header `page,op` and each row has a decimal or `0x` hexadecimal logical page index and `R` or `W`. A trace is **ordered**, and has no timestamps, instruction counts, stalls, process IDs or physical page attribution. Max 200,000 accesses, bounded logical page index. User must supply an authorized, reproducible **real Warden guest trace** later to obtain meaningful title-specific conclusions. The example is only a deterministic synthetic test fixture and must never be quoted as FPS, game memory footprint or actual IOPS.

`--total-mib` is the guest-wide physical memory cap, not process RSS; `--reserve-mib` reserves an assumed amount for kernel, runtime, framebuffer, swap buffers and other nonpage-cache needs. `--frame-meta-bytes` is an **assumed** per-resident-frame metadata overhead. Thus effective cache frames are floor((total-reserve) MiB / (page size + metadata)). This intentionally differs from a real pager that has multiple page sizes and more complex overhead. No inference about actual game memory allocation is possible without real measurements. `--read-us`, `--write-us` are *assumed fixed per-page serialized I/O latency* used for upper-level what-if accounting; no I/O was performed. Swap I/O and CPU processing may overlap in actual systems, so do not interpret the computed sum as observed elapsed time.

Faults count missing pages at access time; dirty-writebacks count evictions of pages written while resident. Read/write bytes scale by the configured page size. No initial file-cache hits or compression/dedup are modeled. The OPT oracle's future knowledge cannot be implemented in a real online operating system. The tool's Python memory consumption is host study overhead, not guest memory; the 512 MiB budget applies only to the modeled frames, and does not enforce Warden physical memory in practice.

**Security/coordination:** read-only trace file; no writes to game devices, assets, processes or Warden kernel. Single new directory plus dedicated workflow; no kernel, graphics, memory or swap agent-owned paths modified. Separate benchmark/owner review must precede any claim of game playability under 512 MiB. This study is not permission to bypass game authentication or anti-cheat.
