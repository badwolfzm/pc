# Doom D1: native Warden ELF memory-layout preflight

Standalone fail-closed **static** validator for a proposed native x86-64 Doom
guest. Checks the currently documented Pong-derived address windows:

| Region | Virtual addresses | Permitted segments |
|---|---|---|
| Code | `0x800000..0x840000` | executable, not writable |
| Data/heap | `0x840000..0x9e0000` | writable, not executable |
| Reserved | `0x9e0000..0xa00000` | no loadable segments (includes stack) |

Run: `python3 tools/doom-elf-budget/audit.py path/to/guest.elf`

Requires GNU `readelf`, Python 3, ELF64/x86-64/ET_EXEC, a nonempty input
up to 32 MiB, no interpreter, dynamic loader or TLS program headers,
nonoverlapping PT_LOAD memory ranges, W^X and an executable entry point.
Failure returns exit status 1 and emits JSON.

Unit tests: `python3 -m unittest discover -s tools/doom-elf-budget -p 'test_*.py' -v`.

**This does not run Doom, validate game semantics, measure FPS/RAM, or enforce
security at runtime.** Native game integration still needs real upstream engine
source, lawful WAD loading, runtime/libc adaptation, input, clock and file
capabilities, kernel-side mapping checks, and two measured one-vCPU QEMU runs.
The current 2 MiB guest arrangement may not fit real Doom; any widening
requires explicit kernel and security-owner review.

Task: #93 (related overlapping D1 milestone issues #89/#91/#94/#95).
Owned paths: `tools/doom-elf-budget/**` only; no shared kernel,
game renderer, storage or contract files touched. Draft for independent review.
