# Genuine Doom native-runtime bundle — 16 actual imports resolved together

This is **integration**, not a new libc provider. It compiles 79 untouched translation units of pinned genuine [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) at commit `e257a09527062bfb986b6e5e0780839b6cbd5b71`, the five merged Warden adapters and **all selected already merged** native CRT/allocator components. One exact-pinned draft [PR #225](https://github.com/badwolfzm/pc/pull/225) provides `snprintf`/`vsnprintf`. PR #218 is a conflicting alternate implementation and is **never staged together**; the lead should choose only one formatting implementation.

The strict aggregate gate checks 16 original unresolved imports disappear: `abs`, `fabs`, `strcasecmp`, `strncasecmp`, `strncpy`, `strstr`, `malloc`, `calloc`, `realloc`, `free`, `strdup`, `__errno_location`, `strtol`, `__ctype_toupper_loc`, `snprintf`, `vsnprintf`. No other baseline imports may disappear; **no new imports** may be introduced. Duplicate global definitions across any provider or pre-existing engine object fail explicitly before linking. `system`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `remove`, `rename` must remain unresolved (never substitute permissive stubs). Builds a **1 MiB candidate static arena** and records the ET_REL section listing, not measured gameplay heap or RAM.

## Reproduce on Linux with pinned source and candidate

```sh
git clone https://github.com/ozkl/doomgeneric.git /tmp/warden-upstream-doom
git -C /tmp/warden-upstream-doom checkout --detach e257a09527062bfb986b6e5e0780839b6cbd5b71
git fetch origin refs/heads/agent/doom-native-bounded-format-20261106
test "$(git rev-parse FETCH_HEAD)" = a3529e43aff95423d0f7c12d78e33ab9e55e3fda
mkdir -p target/doom-runtime-bundle/staged
git show a3529e43aff95423d0f7c12d78e33ab9e55e3fda:tools/doom-native-format/doom_format.c > target/doom-runtime-bundle/staged/format.c
python3 -m unittest discover -s tools/doom-native-runtime-bundle -p 'test_*.py' -v
python3 tools/doom-native-runtime-bundle/compose.py \
  --source /tmp/warden-upstream-doom/doomgeneric \
  --format-source target/doom-runtime-bundle/staged/format.c \
  --output target/doom-runtime-bundle/result
```

GitHub Actions checks exact commits before sourcing external code and archives `result/report.json` plus `result/et-rel-sections.txt`.

**Verification boundary:** the combined object is ELF ET_REL, not a linked executable or a Warden ring-3 game. A 1 MiB arena reservation is not the game's measured heap usage. No native QEMU Doom boot/input/frames, FPS, 512 MiB total guest residency, or Albion Online support is claimed. Real Warden guest still needs full authorized file/config/console services, runtime-compatible formatting (not just imported names), final address-space layout, strong kernel boundary review and two repeated game boots.

Task `doom-native-runtime-bundle-20261106`, coordinated on issue #95. Exclusive new `tools/doom-native-runtime-bundle/**` and dedicated workflow only. No kernel, filesystem, loader, WAD, upstream source, other agent's provider files changed.
