# PR #134: whole-guest 512 MiB evidence validator

This **host-only**, stdlib Python command checks JSON evidence *reported by a
future Warden instrumentation producer*. It is not a game emulator, pager,
performance benchmark, or physical-memory enforcement mechanism.

It rejects a memory budget that counts only game RSS: the 512 MiB goal includes
kernel, runtime/personality, game, framebuffer, paging structures/buffers,
graphics and other physical guest allocations. These exact component keys are
required in every sample. Each sample includes its own sum and monotonic
timestamp. The declared **event high-water** must be at least the maximum sampled total and at most the configured physical capacity. A new required `peak_method` field must equal `physical_frame_event_high_water`; sampled maxima alone are rejected.
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
  sampling may miss peaks. The report must declare continuous frame-event
  accounting and a separately recorded high-water value (which can exceed
  all sampled values). **This declaration is not cryptographic proof**: the
  instrumentation owner must verify collection completeness, exclusivity,
  and the exact capture interval. This tool only checks evidence consistency.
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

## Input integrity controls

The loader rejects reports over 4 MiB (before JSON parsing) and refuses
**duplicate object keys**, including duplicate keys in nested sample objects.
Without these checks an attacker or malformed telemetry producer could silently
replace a reported memory budget or vCPU count with its later duplicate value.
These are data-validation safeguards, not proof of authentic guest telemetry.
The file-size limit bounds input bytes, but a hostile report can still use
CPU/memory during JSON parsing; run untrusted producers in an OS-isolated
context and apply resource limits. The validator is not a sandbox.

## Independent physical-frame event replay

`frame_events.py` validates a bounded offline sequence of 4096-byte physical
frame allocations and releases with explicit numeric frame IDs and mutually
exclusive ownership categories. It reconstructs the **tracked** frame high-water
mark from the submitted event stream. Run:

```sh
python3 tools/ordinary-game-memory-gate/frame_events.py \
  tools/ordinary-game-memory-gate/sample-frame-events.json
python3 -m unittest discover -s tools/ordinary-game-memory-gate -p 'test_*.py' -v
```

It fails on double allocation, stale/unknown free, mismatched category, invalid
frame index, missing origin, reversed timestamps, malformed shapes, negative
counts, and more than 100,000 events. JSON input remains capped at 4 MiB and
duplicate keys are rejected by the shared parser.

**Crucial:** the field `capture_started_empty=true` is a self-reported
assertion, not evidence that instrumentation captured every allocation since
boot. `peak_tracked_frame_bytes` counts only frames mentioned in the log; it
must not be interpreted as the complete guest physical memory peak until a
reviewed native Warden producer attests that all frame sources, reservations,
graphics allocations and other memory are captured. This checker does not
apply page replacement policies, enforce the guest memory limit, collect
QEMU data or run Doom/Albion. Coordinate with guest instrumentation issue
#153 and consolidation issue #155 to avoid parallel runtime changes.
