# Native Doom whole-engine partial-link audit

Issue #167. Compile **80 original pinned Doomgeneric engine C translation units** using freestanding x86-64 System V calling-convention options, then combine them into an actual ELF64 relocatable object using `cc -nostdlib -r`. Count allocatable sections, separate BSS/NOBITS from file-backed bytes, and list undefined symbols of the **combined engine object**.

This goes beyond host-libc linkage and name-only symbol inventories but **is not a native guest executable**: ELF ET_REL does not have a final linker layout, entry point or loaded page mappings. Output intentionally excludes Warden platform adapter, libc runtime, heap provider, WAD assets, linker script, stack and host/guest OS overhead. Sections require final alignment/permissions and may grow at the full link.

```sh
python3 -m unittest discover -s tools/doom-native-link-audit -p 'test_*.py' -v
git clone https://github.com/ozkl/doomgeneric.git /tmp/doomgeneric
git -C /tmp/doomgeneric checkout --detach e257a09527062bfb986b6e5e0780839b6cbd5b71
python3 tools/doom-native-link-audit/audit.py --source /tmp/doomgeneric/doomgeneric --out /tmp/doom-audit
```

The current native Pong guest uses 0x800000–0xA00000 (2 MiB) with data+BSS+heap 0x840000–0x9e0000 (1.625 MiB), but the general Warden user VA range is larger; kernel-owner decisions and actual linker maps determine possible expansion. Exceeding a raw section window is a blocker for the existing layout; fitting does not prove native boot or the 512MiB physical guest budget. No licensed WAD redistribution, QEMU invocation, rendered FPS or Albion support is claimed. Changes are scoped to new tool/workflow files only. Independent review required; do not auto merge.

## Combined Warden adapter size (second diagnostic)

The dedicated CI additionally stages **exact pinned** proposed integration code PR #170 head `4045f7aa1875d73019994a2613184eb688a489b9` into an ephemeral checkout and builds a second actual ET_REL from **79 real upstream Doom units plus five existing Warden adapters**. `audit_combined.py` reads the resulting ELF64 section table, checks its imported symbol list against the independently generated #170 gate report, then saves `combined.json` next to `report.json` for the original 80-unit engine-only build. The comparison is NOT five adapter objects in isolation: the engine-only profile contains the upstream stdio WAD unit whereas the combined profile replaces it. None of the outputs is a final ET_EXEC/PT_LOAD map or a Warden QEMU guest result. Explicitly review the WAD lifetime/security policy and 512 MiB *whole guest* memory bounds before any native launch.

## Strong global symbol provider collision gate

`strong_symbols.py` separately scans **selected actual runtime ELF objects** with `nm -P -g --defined-only` and rejects duplicate strong symbol definitions (exit 1), malformed/missing object inputs (exit 2). Unit tests compile two real C objects defining the same symbol to prove the detector flags collisions. Use this before final native guest linkage to avoid accidentally selecting overlapping PR #175 and PR #177 providers for `strcasecmp`, `strncasecmp`, `strstr` and `strncpy`. Example: `python3 tools/doom-native-link-audit/strong_symbols.py path/to/mini_extra.o path/to/pure.o --report target/symbol-owners.json`. This is not a replacement for a complete final linker duplicate-symbol/ABI check, and doesn't determine policy-safe host imports or runtime correctness.
