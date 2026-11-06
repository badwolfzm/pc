# Source-derived default Doom zone vs candidate arena capacity

Issue [#239](https://github.com/badwolfzm/pc/issues/239), related to real-Doom native guest goal [#95](https://github.com/badwolfzm/pc/issues/95) and [draft PR #238](https://github.com/badwolfzm/pc/pull/238).

This gate parses the immutable pinned upstream `doomgeneric/i_system.c` at `e257a09527062bfb986b6e5e0780839b6cbd5b71`, verifies the default `I_ZoneBase` allocation path uses `DEFAULT_RAM` and `MIN_RAM` in MiB and a malloc of that size, and checks a candidate arena can exceed the zone request. Pinned source currently sets **DEFAULT_RAM = MIN_RAM = 6 MiB**. PR #238 builds a **1 MiB** static arena. A `ld -r` symbol-closure success cannot establish startup viability: `malloc(6 MiB)` cannot succeed with 1 MiB capacity. Even an arena above 6 MiB is only a necessary capacity condition, not an allocator, memory-residency, QEMU, FPS or gameplay proof. Its overhead and other allocations still require room.

Reproduction:
```sh
python3 -m unittest discover -s tools/doom-zone-capacity-gate -p 'test_*.py' -v
python3 tools/doom-zone-capacity-gate/gate.py \
  --source /tmp/pinned-doom/doomgeneric/i_system.c \
  --commit e257a09527062bfb986b6e5e0780839b6cbd5b71 \
  --arena-bytes 1048576
# Expected: nonzero, explicit inadequate capacity (PR #238 candidate)
```

CI first checks the exact upstream git revision, then asserts the 1 MiB candidate is rejected and a synthetic >=8 MiB capacity candidate satisfies this **necessary** constraint. This never changes upstream Doom source, disables its minimum or hacks around a system-call boundary. A real integration must select a bigger *bounded* arena, explicitly authorized separate Doom zone allocation, or a verified application argument and zone behavior, then run actual Warden guest tests and measure complete 512 MiB guest use.

New files only `tools/doom-zone-capacity-gate/**` plus dedicated workflow; no kernel, loader, CRT or allocator-owner modifications. Independent review before merging.
