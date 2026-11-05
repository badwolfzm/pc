# Patches against the uploaded MatrixOS baseline

Per CONTROL.md no thread edits the uploaded C file in place, so fixes to it
are kept here as unified diffs. Apply with `patch -p0 < <file>` on a copy.

| Patch | Fixes | Verified |
|---|---|---|
| 0001-canvas-bench-consumed-hash.patch | `mx_bench_canvas` reported `consumed_hash` 0: it XORed the same `changed_count` and one pixel over 18 identical iterations, so the values cancelled and "output consumed" was vacuous. The patch folds the iteration index in and FNV-1a hashes the whole framebuffer. | Baseline run: CSV row `EV-P61-CANVAS-RT-001` has `consumed_hash` 0. Patched: `4207153748912489638`. The 30-frame demo PPM is unchanged: SHA-256 `99e7937b94f9a5ccbf4dbfaa14cd6314fa7f10d64d63bfc5aae3c183573e4414` before and after (gcc 13, -O2). Regression test `test_consumed_hash_does_not_cancel` in `../test_ghost_canvas.c`. |
