# Guest process-inventory acceptance gate

Issue #193. This is an **offline evidence-contract validator**, NOT a process
monitor and NOT an attestation that Doom or Albion Online has booted.

Run: `python3 -m unittest discover -s tools/guest-game-process-inventory -p 'test_*.py'`.
For a JSON report: `python3 tools/guest-game-process-inventory/check.py report.json`.

The strict experimental profile demands exactly one QEMU guest vCPU, a
complete ordered userspace process lifecycle, **at most one concurrently
running guest userspace process**, and exactly one game PID during the boot.
Sequential helper processes are permitted; if the requirement is literally
one userspace PID for the whole boot, disallow helpers separately. Kernel
threads and the QEMU host's processes are NOT counted. A one-vCPU guest does
not imply a one-process guest. A one-process guest does not imply a one-thread
guest.

Schema 1: exactly `schema:1`, `source:"synthetic"|"guest-reported"`,
`boot_id` (64 lowercase hex), `complete:true`, `vcpu_count:1`,
and `events`. Events are exact objects with contiguous zero-based `seq`,
`event:"boot"|"spawn"|"exit"|"end"`, plus positive integer `pid` and
`role:"game"|"helper"` for spawn/exit. The boot and end events bracket a
complete lifecycle. No PID reuse, concurrent extra process or missing exit.
The JSON limit is 1 MiB/10,000 events.

**Limitations:** Any producer can forge `complete`, events or the boot ID.
Independent trusted in-guest instrumentation, trace continuity, and
cross-checks against the Warden guest scheduler are needed before claiming
real process isolation. No RAM, shared VRAM, timing, FPS or native gameplay
measurements are performed. The RAM/VRAM accounting work is owned separately
(#190), and memory validators (#141/#142/#146) are not replaced. No kernel
or guest ABI files are changed. Independent review needed before merge.
