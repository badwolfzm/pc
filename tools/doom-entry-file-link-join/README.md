# Genuine Doom entry + read-only FILE link integration

**Task:** `doom-entry-file-link-join-20261106`. Owner: current user-authorized engineering session. Owned paths: `tools/doom-entry-file-link-join/**` and dedicated CI workflow only. Independent review required.

This is a **real source ET_REL partial-link integration gate**, not a new libc provider or another fake Doom engine. It combines the actual 79-source pinned Doomgeneric engine and Warden runtime produced by merged `tools/doom-native-runtime-bundle/compose.py`, the **already merged** native Warden `_start` and `exit` sources, and the real read-only `FILE` implementation from open PR #264, pinned at commit `cd6e6fbc4b69320acc8cef5ca6ed5425d5de8909` without merging it.

The checker enforces an exact disappearance of `exit`, `fopen`, `fclose`, `fread`, `fseek`, and `ftell`. It requires `_start`, forbids duplicate global definitions and new undefined imports, and **keeps** `system`, `fwrite`, `remove`, and `rename` unresolved. Evidence includes SHA-256 of each source/object and remaining unresolved symbols. Any unexpected import change fails.

Local unit tests: `python3 -m unittest discover -s tools/doom-entry-file-link-join -p 'test_*.py' -v` (10 tests pass on a Linux container). The complete pinned upstream compilation and join are **CI-only, not yet verified** until the workflow passes. The source pin in this branch is not independent approval of PR #264.

**Not implemented:** no Warden kernel capability grant for the IWAD, no actual asset read callback hookup, no final ET_EXEC link, no loader/boot, input/gameplay, graphics, memory/FPS measurements or Albion Online. Even if the partial link succeeds, it does not prove runtime correctness or that the 512 MiB Warden guest budget is met.

Dependencies: merged runtime and native entry; PR #264 independent review and integration lead authorization. Next: inspect `result/report.json` exact remaining real imports, authorize FILE provider semantics and wire the real WAD read grant, then reattempt final executable link through the existing final-link diagnostic (#242/#246).