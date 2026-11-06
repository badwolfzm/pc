# Genuine Doom native-runtime bundle — 16 actual imports resolved together

This is **integration**, not a new libc provider. **The original merged PR #238 used a 1 MiB arena despite a 6 MiB source-derived Doom startup zone. This revision replaces that nonviable default with the already host-validated 8 MiB candidate.** It compiles 79 untouched translation units of pinned genuine [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) at commit `e257a09527062bfb986b6e5e0780839b6cbd5b71`, the five merged Warden adapters and **all selected already merged** native CRT/allocator components. One exact-pinned **merged** [PR #225](https://github.com/badwolfzm/pc/pull/225) provides `snprintf`/`vsnprintf`. PR #218 is a conflicting alternate implementation and is **never staged together**; PR #225 was merged as the selected canonical implementation.

The strict aggregate gate checks 16 original unresolved imports disappear: `abs`, `fabs`, `strcasecmp`, `strncasecmp`, `strncpy`, `strstr`, `malloc`, `calloc`, `realloc`, `free`, `strdup`, `__errno_location`, `strtol`, `__ctype_toupper_loc`, `snprintf`, `vsnprintf`. No other baseline imports may disappear; **no new imports** may be introduced. Duplicate global definitions across any provider or pre-existing engine object fail explicitly before linking. `system`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `remove`, `rename` must remain unresolved (never substitute permissive stubs). Builds an **8 MiB bounded static arena**. The pinned genuine Doom engine's startup code requests a **6 MiB zone**, and a separately host-tested allocation of that zone plus **1 MiB scratch** fits in the selected 8 MiB arena. CI validates both the real allocations under UBSan and the linked 8 MiB BSS using the ELF section parser. These are *necessary startup-capacity conditions*, not measured gameplay heap or RAM.

## Reproduce on Linux with pinned source and candidate

```sh
git clone https://github.com/ozkl/doomgeneric.git /tmp/warden-upstream-doom
git -C /tmp/warden-upstream-doom checkout --detach e257a09527062bfb986b6e5e0780839b6cbd5b71
git fetch origin main
git merge-base --is-ancestor 4246bde3e786f1c51e3b047292a592cb5c057bf1 FETCH_HEAD
mkdir -p target/doom-runtime-bundle/staged
git show 4246bde3e786f1c51e3b047292a592cb5c057bf1:tools/doom-native-format/doom_format.c > target/doom-runtime-bundle/staged/format.c
python3 -m unittest discover -s tools/doom-native-runtime-bundle -p 'test_*.py' -v
python3 tools/doom-native-runtime-bundle/compose.py \
  --source /tmp/warden-upstream-doom/doomgeneric \
  --format-source target/doom-runtime-bundle/staged/format.c \
  --output target/doom-runtime-bundle/result
```

GitHub Actions verifies the pinned merged formatter revision is in live main history before staging its exact file and archives `result/report.json` plus `result/et-rel-sections.txt`.

**Verification boundary:** the combined object is ELF ET_REL, not a linked executable or a Warden ring-3 game. An 8 MiB arena reservation is not the game's measured heap usage. Even a successfully allocated host-side 6 MiB zone does not mean Doom has booted inside Warden. No native QEMU Doom boot/input/frames, FPS, 512 MiB total guest residency, or Albion Online support is claimed. Real Warden guest still needs full authorized file/config/console services, runtime-compatible formatting (not just imported names), final address-space layout, strong kernel boundary review and two repeated game boots.

The formatter source is pinned to merged main commit `4246bde3e786f1c51e3b047292a592cb5c057bf1` and remains accessible if the old PR feature branch is deleted.

Task `doom-native-runtime-bundle-20261106`, coordinated on issue #95. Exclusive new `tools/doom-native-runtime-bundle/**` and dedicated workflow only. No kernel, filesystem, loader, WAD, upstream source, other agent's provider files changed.

## 8 MiB startup-profile acceptance and follow-up

The fixed default `ARENA_BYTES = 8*1024*1024` in `compose.py` is intentionally fail-closed: unreviewed capacities (including the old 1 MiB setting) are refused by `validate_profile`. It is compiled into the real linked arena via `-DDOOM_ARENA_BYTES=8388608` and checked against actual `SHF_ALLOC` / `SHT_NOBITS` ELF sections. CI additionally runs:

```sh
python3 tools/doom-zone-capacity-gate/gate.py \
    --source /tmp/warden-upstream-doom/doomgeneric/i_system.c \
    --commit e257a09527062bfb986b6e5e0780839b6cbd5b71 \
    --arena-bytes 8388608
cc -std=c11 -O2 -DDOOM_ARENA_BYTES=8388608 \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -I tools/doom-freestanding-arena \
    tools/doom-freestanding-arena/arena.c \
    tools/doom-eight-mib-zone/test_zone.c -o /tmp/doomarena8
/tmp/doomarena8
```

**Dependent-tool coordination:** `tools/doom-eight-mib-zone/profile.py` was developed as an earlier isolated prototype and assumes literal 1 MiB constants in the canonical composer to temporarily transform them to 8 MiB. Once this canonical profile is adopted, that temporary source rewrite will fail closed and must be removed/reworked **by that tool's owner** before integration; do not silently ignore the dependent workflow. This PR intentionally does not edit another agent's paths.

**Security boundary:** [issue #263](https://github.com/badwolfzm/pc/issues/263) documents that the existing native kernel console-write path checks Endpoint kind and WRITE rights but not designated console-object identity. This runtime bundle does not resolve that kernel authorization defect. A successful symbol or memory test is not a security clearance or an actual native Doom game boot.
