# Single-core host resource-envelope probe (Issue #90)

**Scope: Linux host process only.** This tool measures command completion under an
OS CPU-affinity restriction and `RLIMIT_AS` virtual-address-space cap. It is not
Warden, QEMU, Doom, Albion, a frame-rate test, or a secure process sandbox.

Run from this directory:

```sh
python3 -m unittest -v test_run.py
python3 run.py --memory-mib 256 --seconds 5 --cpu 0 -- /bin/true
```

Use a CPU number from `taskset -pc $$` (CPU 0 may be unavailable in a container).
JSON records selected CPU, virtual memory cap, elapsed wall time, child exit
status and timeout. No assertion of resident RAM, frame rate, or single process
is made: `RLIMIT_AS` caps virtual mappings, not physical resident RAM; a child
may spawn children; and `killpg` may not reach escaped descendants. `preexec_fn`
should be used only from a single-threaded Python parent; do not embed this in
a multi-threaded service. The tool is for trusted test programs, not adversarial
code. It suppresses stdout/stderr; capture output separately for debugging.

For real single-vCPU guest acceptance, use QEMU `-smp 1`, independent memory
accounting, and the native Warden game acceptance tests (PR #80). For actual
Doom, integrate and validate a legally obtained Doom engine and WAD assets
(PR #88), and separately verify guest loader, files, libc, and graphics. Albion
requires much broader Linux syscall, GPU, dynamic linkage, networking, and
vendor/anti-cheat compatibility; this tool alone establishes none of these.

No kernel, user ABI, graphics, game probe, or existing agent-owned files changed.
