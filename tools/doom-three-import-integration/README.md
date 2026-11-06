# Real Doom: combined three-import closure (not a guest boot)

This is an **integration test**, not another implementation of a C runtime.
It combines three independently developed freestanding components in a
single real Doomgeneric upstream relocatable link:

- Merged `tools/doom-native-ctype/doom_ctype.c` (`__ctype_toupper_loc`)
- Exact pinned **draft PR #208** source at
  `fae610a1f640e232499f6d86b980640e8559c420` (`__errno_location`)
- Merged `tools/doom-native-strtol/strtol.c` (`strtol`)

The GitHub job compiles all **79 real original engine source units** from
`ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`
plus the existing Warden adapters. It checks each provider's defined and
undefined symbols, then partial-links all three with `ld -r`. Exactly
three original imports must disappear and no other import may change;
host process/filesystem imports must stay unresolved.

**The result is one ET_REL file, not a runnable executable.**
No test here boots Warden/QEMU, runs the game, measures 512 MiB total
physical RAM or FPS, or establishes Albion compatibility. The errno provider
is single-thread/per-static-image only. CI reads PR #208's exact commit
without modifying its branch; it must not be merged or integrated into
general Linux personality without runtime-owner review.

Acceptance: exact-head CI passes original pinned engine, negative linker
tests, and source-provenance verification. The kernel/security owner must
review runtime contract before any merge. Independent follow-up must
produce fully linked ELF, legitimate WAD access and native gameplay evidence.

Task #211 · exclusively owns `tools/doom-three-import-integration/**` and
its dedicated workflow; does not edit any existing CRT, engine, adapter,
kernel, loader, graphics, or WAD owner files.
