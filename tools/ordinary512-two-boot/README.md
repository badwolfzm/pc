# Two independent native Warden 512 MiB / 1-vCPU Pong boots

A **stacked follow-on to PR #154**: one QEMU guest boot is valuable but the
merged PR #134 research specification requires repeatability. This isolated
tool checks **two separately recorded** native Pong serial runs, validates each
against the exact PR #154 native evidence parser, checks one-vCPU/512 MiB
configuration and 15/15 kernel isolation tests, and records framebuffer
hashes and whether they agree. It preserves both raw logs, the manifest, and one combined JSON file.

The dedicated CI workflow runs the **actual native Warden Pong guest** twice
sequentially with QEMU `-m 512M -smp 1`, rather than replaying sample logs.
It writes separate serial files/screenshots and retains both as artifacts.
The `prepare.py` alteration is temporary inside the CI checkout and is not
a kernel or Pong script commit. Byte-identical logs are permitted because
deterministic runs can legitimately produce identical output. Conversely, Pong's
host-timed QEMU-monitor keyboard injection can change the final screen state
between otherwise successful runs. Neither identical nor different final hashes
prove independent launches; CI's separate QEMU invocations provide that evidence.
In GitHub Actions run 42197445583 both native Pong boots and their individual
15/15 kernel checks passed, but the first version of this tool incorrectly
rejected two different final hashes. Corrected checker treats the difference as
an explicit observation. Per-run in-kernel framebuffer correctness is still
verified by the existing Pong test.

This is a real native Warden/QEMU test **of Pong**, not actual Doom or ordinary
modern-PC games. A virtual-memory size option is not measured peak physical
residency; Pong's kernel-reported allocator frames omit some memory categories.
No page-reference trace, real game FPS, 300 rendered-frame digest sequence,
CPU resource ceiling, actual swap I/O or compatibility with Albion is claimed.
Separate gameplay/loader evidence remains necessary.

Only new `tools/ordinary512-two-boot/**` and dedicated CI are owned here.
Based on merged PR #154. Require exact-head CI and owner review before merging.
