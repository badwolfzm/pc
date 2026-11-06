# 512 MiB modern-PC game working-set trace feasibility

Host-only diagnostic implementing a falsifiable work package from merged PR #134 (ordinary modern PC games, **one vCPU / 512 MiB physical guest machine**). It does not run or modify a game or enforce a memory cap.

Input is ASCII lines of `timestamp_us page_id`, with nondecreasing timestamp and unsigned decimal 4 KiB page identifiers (one trace identity namespace). # comments/blank lines allowed. Input cap: 64 MiB; event cap: one million. The reference count can be smaller than a real game run.

```sh
python3 -m unittest discover -s tools/warden-512-working-set -p 'test_*.py' -v
python3 tools/warden-512-working-set/trace.py access.trace --non-game-reserved-mib 64
```

The user-specified non-game RAM reservation must cover the kernel, drivers, services, framebuffer, page tables, compression metadata, etc. **64 MiB in the example is arbitrary, not a measured Warden figure.** With 4 KiB pages, the resident game-page allowance is `(512 - reserved_MiB) * 256` pages. Output is JSON.

Compares LRU page-fault count against the **offline future-knowing Belady minimum** on the same reference order, assuming an initially empty cache. The latter is an unattainable online ideal unless future accesses are known. Window unique-page counts (default 16 ms bins) flag pressure but **are not a proof of simultaneous residency, physical impossibility, missed deadlines, real swap IOPS or FPS**. Pages can be accessed sequentially with faults, and runtime page faults consume one-core CPU time and storage bandwidth. Accurate results require consented real game traces and complete guest memory accounting.

No overlap with `tools/warden-swap-probe` dirty-page swap reference, `tools/game-budget-gate` evidence-report validator, or `tools/game-host-envelope` host process cap runner. Does not touch shared kernel/loader, APIs, asset drivers, graphics or any agent-owned path.

Task #144, branch `agent/warden-512-working-set-20261106`. Host unit tests only; no Warden guest, QEMU, Sims, Doom or Albion test. Independent review and control-lead integration required; no automatic merge.
