# Ordinary modern PC games: 512 MiB page-trace feasibility model

Tracks issue #136 and implements one independent **offline** analysis gate for
merged vision PR #134. No kernel/storage/security, benchmark, or Warden guest
files changed.

Usage: `python3 tools/ordinary-pc-512-trace/trace_model.py path/to/trace.json`.
Unit tests: `python3 -m unittest discover -s tools/ordinary-pc-512-trace -p 'test_*.py' -v`.

Input JSON is an object with `non_game_resident_bytes` mapping named components
(kernel, page tables, framebuffers, runtime, DMA buffers, etc.) to their byte
reservations, plus a time-sorted `events` list. Every event must have integer
`t_ms`, integer virtual `page` identifier (4 KiB units), and boolean
`write`. A trace can come from a separately authorized instrumented guest
but this tool NEVER captures traces itself or accesses storage devices.

The simulator deducts all non-game reservations from the **512 MiB total guest
physical RAM** and uses a bounded LRU page cache for the remainder. Reports
modeled cold page-ins, dirty evictions, counts and distinct pages/100ms.
Nonresident pages can live in virtual address space or authorized backing;
this model DOES NOT implement virtual memory, swap or CPU rendering.

**Limitations:** an offline model cannot prove feasibility or impossibility of
meeting frame deadlines. Distinct pages accessed within 100ms are not all
necessarily needed concurrently; misses include first loads and are not
necessarily swap I/O. There is no modeled I/O latency, compression, memory
allocation during fault handling, CPU pressure, graphics, or FPS. Do not use
the result to claim that any actual game runs in 512 MiB. For a genuine game,
measure two one-vCPU QEMU boots with actual memory accounting and frames.

Input capped at 8 MiB / 200,000 events; bad types, unsorted timestamps,
oversubscribed RAM reservations and invalid page IDs fail closed.
