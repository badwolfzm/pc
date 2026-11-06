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
counts, and more than 300,000 events. Memory snapshot reports retain the
4 MiB JSON limit; physical-frame event reports instead use a separate 32 MiB
limit so a full 512 MiB / 4 KiB = 131,072-frame allocation and release
lifecycle (262,144 events) can be represented. Duplicate object keys remain
rejected in both formats. The full-population test uses synthetic allocations
and releases; it is **not** evidence of a 512 MiB Warden guest high-water mark.

**Crucial:** the field `capture_started_empty=true` is a self-reported
assertion, not evidence that instrumentation captured every allocation since
boot. `peak_tracked_frame_bytes` counts only frames mentioned in the log; it
must not be interpreted as the complete guest physical memory peak until a
reviewed native Warden producer attests that all frame sources, reservations,
graphics allocations and other memory are captured. This checker does not
apply page replacement policies, enforce the guest memory limit, collect
QEMU data or run Doom/Albion. Coordinate with guest instrumentation issue
#153 and consolidation issue #155 to avoid parallel runtime changes.

## Cross-report reconciliation

The snapshot validator and event-replay validator can individually accept two
**contradictory** synthetic reports. `reconcile.py` provides an optional
stronger gate that requires the *same* guest capacity in both reports,
a snapshot high-water equal to the event-reconstructed peak, and exact
per-category resident-frame byte totals at **each** sample time.
Each sample with timestamp `time_ms` includes all frame events at or before
`time_ms * 1,000,000` nanoseconds. The final sample must cover every event.
A frame cannot simultaneously occupy two physical ownership categories.

```sh
python3 tools/ordinary-game-memory-gate/reconcile.py \
  snapshots.json frame-events.json
python3 -m unittest discover -s tools/ordinary-game-memory-gate -p 'test_*.py' -v
```

This mode requires **page-granular** exclusive category accounting in the
snapshot (multiples of 4096 bytes), an event log beginning before any frame
allocation, and a consistent monotonic clock between streams. Unlike the
general sample gate, it deliberately rejects partially overlapping accounting
or a trace that ends before its last unreported allocation. A valid synthetic
report is still not proof that the native guest telemetry producer captured all
kernel frames, hardware reservations or device memory. Provenance and
physical-guest integrity remain an owner-reviewed integration task (#153).

## Initial reserved physical frames (schema v2)

Schema v1 still requires `capture_started_empty=true` and assumes every
allocated frame appears in the event sequence. Actual kernel instrumentation
may start after bootstrap reservations have already been made. Schema v2
can express this explicitly, adding `capture_start_ns` and an
`initial_frames` list while setting `capture_started_empty=false`.
The list contains distinct `{"frame_id": integer, "kind": category}` items,
bounded by the configured number of physical frames. Allocation of an already
reserved frame is rejected; valid later release is allowed. The initial
inventory contributes to peak and live resident counts, and reconciliation
starts with those category counts and forbids samples before capture begins.

This new mode only verifies **consistency of the declared initial inventory**.
It does not independently measure or authenticate the kernel's boot memory
reservations; omitting a reserved frame can still underreport memory.
Until native Warden producer ownership (#153) establishes complete provenance,
neither v1 nor v2 can support verified whole-guest RAM claims.
