# PR #134: whole-guest 512 MiB evidence validator

This **host-only**, stdlib Python command checks JSON evidence *reported by a
future Warden instrumentation producer*. It is not a game emulator, pager,
performance benchmark, or physical-memory enforcement mechanism.

It rejects a memory budget that counts only game RSS: the 512 MiB goal includes
kernel, runtime/personality, game, framebuffer, paging structures/buffers,
graphics and other physical guest allocations. These exact component keys are
required in every sample. Each sample includes its own sum and monotonic
timestamp. The declared observed peak must equal the maximum sample total.
The configured physical guest capacity must be <= 536870912 bytes, with one
guest vCPU; host peak RSS is recorded separately, not added to guest memory.

Run:
```sh
cd tools/ordinary-game-memory-gate
python3 -m unittest -v test_check.py
python3 check.py sample.json
```

Outputs JSON with `accepted` true/false and reason, with an exit code of 0/1.
The included fixture is **synthetic** for checking schema and validation
behavior, not a real Warden game measurement.

Important caveats:
- Two or more samples alone **cannot prove a true transient high-water mark**;
  sampling may miss peaks. The instrumentation owner must supply a reliable
  allocator/page accounting high-water and exact capture interval, with
  independent verification. This tool only validates declared evidence shape.
- Identical physical pages mapped in multiple components must be attributed
  once, not double-counted. Producers must define exclusivity, memory overhead,
  and which buffers are attributed to each component.
- A configured 512 MiB physical guest limit does not itself prove the host
  process fits 512 MiB. Both budgets must be measured separately.
- No claim that any modern game boots or has sufficient CPU, graphics, I/O,
  network, or RAM performance.
- The current validator intentionally **does not accept** memory usage
  computed from process virtual address space or an estimated swap file.

Task #137, complements the game FPS gate #70 and merged PR #134.
Requires independent review and a native Warden producer before use as
project acceptance evidence. No existing kernel or agent-owned code changed.
