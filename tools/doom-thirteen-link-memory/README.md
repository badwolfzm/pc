# Real Doom: 13 merged native imports and allocatable ELF sections

Combines **current merged main** runtime sources only. Reproducibly builds
79 original pinned Doomgeneric engine translation units and five actual
Warden adapters using `tools/doom-native-link-gate/gate.py`, then partially
links seven merged source files (pure math, extra strings, fixed 1 MiB heap,
heap C allocation aliases, character conversion, single-thread errno, strtol).

Strong symbols are checked across **all selected objects** before linkage,
and the genuine baseline's undefined imports must lose exactly 13 known
names, preserving all system/file authorities unresolved. Unlike a name-only
simulator, the output is an actual combined x86-64 ELF64 `ET_REL` object.

The report reads the binary's **SHF_ALLOC** section headers and separately
totals on-disk PROGBITS-like allocations and zero-fill/NOBITS allocation,
including the 1 MiB static heap candidate. These are source-backed linker
section sizes, **NOT a Warden virtual-memory map or physical-resident peak**.
Final executable section layout, page rounding, stacks, memory service,
WAD bytes, shared GPU reservation and hardware driver remain unmeasured.

No hosted libc is linked and host dangerous operations remain unresolved.
This is intentionally not ET_EXEC, not a native gameplay boot, not FPS and
not proof that 512 MiB guest RAM works. A static single-thread errno
provider is not safe for general multithreaded Linux/Albion software.

Run `python3 -m unittest discover -s tools/doom-thirteen-link-memory -p 'test_*.py' -v`
locally; the dedicated CI pins authentic upstream source and records
`target/doom-thirteen/report.json`.

Issue #228, exclusive new paths `tools/doom-thirteen-link-memory/**`
and dedicated workflow. Independent linker/security review before merge.
