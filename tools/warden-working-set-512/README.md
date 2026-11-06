# One-core / 512 MiB game working-set trace analysis

An isolated, **host-only** measurement and reproducibility aid for merged vision PR #134 (`docs/ordinary-pc-game-512mb-vision.md`). Given a chronological **authorized** page-reference CSV, it simulates a strict-size LRU resident page cache and reports unique page counts over rolling 16/100/1000 ms windows, faults, evictions and dirty writebacks.

## Quick start

```sh
python3 -m unittest discover -s tools/warden-working-set-512 -p 'test_*.py' -v
python3 tools/warden-working-set-512/analyze.py trace.csv --total-mib 512 --reserved-mib 128
```

Trace CSV must have exactly `time_ms,page_id,access` as header; integer nonnegative timestamps in ascending order, integer nonnegative page IDs, and `R` or `W` access. `page_id` is a **normalized unique page identity**, not raw host virtual address; trace providers must define and preserve address-space and mapping identity across processes and remaps. Reused numeric addresses representing different mappings must have different IDs. Times are milliseconds on a single monotonic clock. The tool uses 4 KiB pages by default.

512 MiB **includes** Warden kernel, runtime, framebuffers, metadata and any compression/swap buffers. `--reserved-mib` subtracts those non-game costs; **128 MiB is only an illustrative reservation, never a measured Warden footprint**. The remaining 384 MiB (98,304 x 4 KiB pages) is an estimate of game-resident physical capacity, not an asserted actual quota.

Outputs JSON for deterministic comparisons; no userspace code is executed, no storage device probed, and no filesystem is accessed other than the explicitly passed trace file. Working-set uniqueness above available pages in a time window signals potential paging pressure **not a strict impossibility**, since pages might be consumed sequentially. LRU results are reference-model simulation, not kernel swap proof: no page latency, IOPS, data integrity, compression, scheduler, GPU/API behavior, or real game execution is measured. No FPS, RAM measurements or game compatibility claims result from synthetic traces. Host analysis memory usage grows with trace uniqueness and should not be counted against the guest budget.

Next: a separately owned trace exporter with capability-authorized page IDs and measured kernel/non-game reservation, a trace corpus from lawful ordinary modern-PC games, real I/O latency modeling, and native QEMU `-smp 1 -m 512M` correctness tests. Coordinates with #92 and PR #82; does not edit swap or kernel code.

**Scope:** only `tools/warden-working-set-512/**` and `.github/workflows/warden-working-set-512.yml`. Do not merge without review of trace semantics and resource accounting.
