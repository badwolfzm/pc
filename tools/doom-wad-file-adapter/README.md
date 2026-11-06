# Doom WAD sequential file adapter (one-vCPU G1 prerequisite)

Thin, fixed-memory, read-only wrapper on top of the merged `tools/doom-wad-stream/` reader. Adds file-like open, exact-length read, tell, and signed seek from start/current/end, including checks for INT64_MIN and INT64_MAX. No heap, file descriptor table, storage enumeration, raw device writes, or network access.

The caller MUST already possess a vetted, immutable, capability-authorized `read_at` implementation and trusted total byte length. Callbacks must deliver the full read or fail and must not asynchronously change the WAD. Do not use these APIs as a substitute for provenance checking or hard security isolation. The source WAD itself is not included.

This is a **candidate for binding into upstream Doom's WAD I/O functions**, not an override of libc's `fopen`/`fread`, not full stdio and **not actual Doom execution**. The future engine port must trace how Doom uses file handles, ordinary files, paths and write calls; route only the declared read-only WAD source here.

Validation: `make -C tools/doom-wad-file-adapter test`; `make -C tools/doom-wad-file-adapter freestanding`; CI also uses UBSan. Native Warden guest startup and memory/FPS measurements are NOT covered.

Scope: new `tools/doom-wad-file-adapter/**` and dedicated CI workflow only. Coordinates with issue #95, merged PR #103, open PR #88; no modifications to parallel WAD directory PR #104, kernel, framebuffer, or other agents' claimed files.
