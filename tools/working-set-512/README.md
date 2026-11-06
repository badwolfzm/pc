# Working-set trace model for PR #134 (one vCPU, 512 MiB)

This read-only Python stdlib tool estimates memory pressure for a *supplied*
page-reference trace. It is not a page tracer, pager, Warden guest, or game
benchmark. No device scan, privileged access or swap file writes.

Input: UTF-8 JSON <=8 MiB with 1..150000 `events`, each an object with
nonnegative integer `t_ms` (nondecreasing) and `page` (4 KiB virtual page
identity). Example:

```json
{"events":[{"t_ms":0,"page":1},{"t_ms":5,"page":2},{"t_ms":9,"page":1},{"t_ms":20,"page":3}]}
```

Run:

```sh
python3 -m unittest discover -s tools/working-set-512 -p 'test_*.py' -v
python3 tools/working-set-512/analyze.py my-trace.json --ram-mib 512 --reserved-mib 128
```

A profile enforces total guest RAM <=512 MiB, with `reserved_mib` explicitly
covering kernel, personality server, framebuffer, pinned pager, metadata and
other system memory. Default available traced-page pool = 384 MiB = 98304
physical frames. This is *not* per-process RSS. Do not use zero reserve when
claiming a whole-guest experiment.

Results: distinct pages in 16/100/1000 ms trailing windows, unique 4-KiB
page IDs, simulated LRU cache misses and theoretical Belady MIN/offline
optimal misses. **MIN requires advance knowledge of future accesses** and is
not a practical online scheduler. The differences do not imply an achievable
FPS benefit. Clean file-backed pages, dirty writebacks, true I/O latency,
compression, deduplication, shared physical pages, resident critical system
pages, and trace instrumentation overhead are not modeled. Equal virtual page
IDs across processes must be namespaced by the trace producer.

The trace must originate from consented/authorized workloads and distinguish
host instrumentation from genuine Warden guest evidence. This analyzer does
not establish that any game will fit, meet deadlines, or use graphics correctly.
No actual game, Warden QEMU, physical storage, or 512 MiB performance tests
have been run here.

Claim: ordinary-pc-512mb-working-set-20261106; issue #139; scoped new
`tools/working-set-512/**` only. Coordinate with PR #134 and issue #92.
Local Python 3 test suite: seven tests passed before publication. Independent
review and real trace producer are future work; no automatic merge.
