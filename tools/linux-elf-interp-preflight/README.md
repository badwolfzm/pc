# Linux ELF PT_INTERP metadata preflight (host-side only)

Bounded, fail-closed parser for ELF64 little-endian x86-64 ET_EXEC/ET_DYN metadata. Reports the requested PT_INTERP string without opening or executing it. Rejects malformed program-header ranges, duplicate/oversized/noncanonical interpreter names, W+X PT_LOAD, invalid alignments, overlaps, and out-of-range segment bytes. No filesystem access is performed except reading the explicitly supplied input ELF (max 64 MiB).

Run: `python3 check.py /path/to/authorized/elf`
Tests: `cd tools/linux-elf-interp-preflight && python3 -m unittest -v`

**Security boundary:** This is not a Warden loader, ELF signature validator, syscall personality, file grant, path resolver, relocator, or runtime compatibility proof. Absolute interpreter paths are metadata, **not** authority to open host paths. The eventual Warden loader must resolve interpreter paths through a capability-rooted, race-resistant guest filesystem, enforce mapping/entry/relocation policy, and account memory. The parser does not validate all ELF fields or loader semantics and must not be used as an executable authorization gate.

**Evidence (2026-11-10):** Nine Python unittest cases passed on isolated host-side Python 3 prior to publishing this branch. No GitHub Actions or QEMU execution is claimed; rerun exact published head and request independent review. This does not advance the Doom native ET_EXEC link blocker in PR #282. Scoped to issue #29, with no kernel/ABI modifications.
