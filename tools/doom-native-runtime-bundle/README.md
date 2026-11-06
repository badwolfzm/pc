# Genuine Doom native-runtime bundle — 16 actual imports resolved together

This is **integration**, not a new libc provider. It compiles 79 untouched translation units of pinned genuine [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) at commit `e257a09527062bfb986b6e5e0780839b6cbd5b71`, the five merged Warden adapters and **all selected already merged** native CRT/allocator components. One exact-pinned **merged** [PR #225](https://github.com/badwolfzm/pc/pull/225) provides `snprintf`/`vsnprintf`. PR #218 is a conflicting alternate implementation and is **never staged together**; PR #225 was merged as the selected canonical implementation.

The strict aggregate gate checks 16 original unresolved imports disappear: `abs`, `fabs`, `strcasecmp`, `strncasecmp`, `strncpy`, `strstr`, `malloc`, `calloc`, `realloc`, `free`, `strdup`, `__errno_location`, `strtol`, `__ctype_toupper_loc`, `snprintf`, `vsnprintf`. No other baseline imports may disappear; **no new imports** may be introduced. Duplicate global definitions across any provider or pre-existing engine object fail explicitly before linking. `system`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `remove`, `rename` must remain unresolved (never substitute permissive stubs). Builds a **1 MiB candidate static arena** and records the ET_REL section listing, not measured gameplay heap or RAM.

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

**Verification boundary:** the combined object is ELF ET_REL, not a linked executable or a Warden ring-3 game. A 1 MiB arena reservation is not the game's measured heap usage. No native QEMU Doom boot/input/frames, FPS, 512 MiB total guest residency, or Albion Online support is claimed. Real Warden guest still needs full authorized file/config/console services, runtime-compatible formatting (not just imported names), final address-space layout, strong kernel boundary review and two repeated game boots.

The formatter source is pinned to merged main commit `4246bde3e786f1c51e3b047292a592cb5c057bf1` and remains accessible if the old PR feature branch is deleted.

Task `doom-native-runtime-bundle-20261106`, coordinated on issue #95. Exclusive new `tools/doom-native-runtime-bundle/**` and dedicated workflow only. No kernel, filesystem, loader, WAD, upstream source, other agent's provider files changed.
