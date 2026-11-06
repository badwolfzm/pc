# One-vCPU / 512 MiB ordinary-PC game trace-replay research

Standalone offline working-set probe for PR #134's *ordinary modern integrated-graphics PC game* research goal. It is **not** a hypervisor, kernel pager, graphics backend, game emulator, FPS estimator, proof of universal compatibility or RAM measurement.

A trace is UTF-8 JSON with `guest_ram_bytes` fixed to **536870912** (512 MiB), `reserved_bytes` containing exactly `kernel`, `runtime`, `framebuffers`, `cache`, and `metadata` (all 4KiB aligned), and `events` of `{"ms":0,"page":123,"write":false}` with monotonic nonnegative milliseconds. Page IDs must be **actual guest virtual page indices** from a lawful trace collector; page numbers are not physical-frame IDs. A simulated LRU resident set uses leftover physical-page capacity. This does not automatically collect game traces.

```sh
python3 -m unittest discover -s tools/game-working-set-probe -p 'test_*.py' -v
python3 tools/game-working-set-probe/replay.py tools/game-working-set-probe/example.json
```

Output includes page faults, evictions, modeled dirty writebacks and page-in bytes, plus distinct pages touched in wall-clock-aligned 16/100/1000 ms buckets. All values are **model predictions** driven by trace events; actual swap disk behavior, compression, CPU overhead, spatial sharing, page tables, pinned resident allocations, prefetch and I/O scheduling are not represented except through explicit reservations. Every unique page touched within a window need **not** be resident simultaneously. A window with more unique pages than capacity indicates potential page churn but does not prove a game's execution is impossible.

Security and trace limitations: 4 MiB maximum JSON, 100k events, explicit schema, strictly 512 MiB profile; empty traces supported. The model does not enforce host CPU affinity, one guest process, real `-smp 1`, authorization of swap devices, or performance thresholds. Never use synthetic input as measured game evidence.

Claim #135; new paths only `tools/game-working-set-probe/**` plus dedicated CI workflow. Depends on PR #134; independent review and lead integration required before merging. Keep draft.
