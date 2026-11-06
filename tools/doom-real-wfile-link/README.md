# Pinned, real upstream Doom file API linkage smoke

This tests **unmodified, actual** `doomgeneric/w_file.c` from upstream `ozkl/doomgeneric` commit `e257a09527062bfb986b6e5e0780839b6cbd5b71`, compiled with upstream's genuine `w_file.h` and `doomtype.h`. GitHub CI fetches that revision and verifies `git rev-parse HEAD` exactly.

The original upstream `w_file.c` hardcodes a global `stdc_wad_file` backend for its default non-mmap path. Here `bridge_backend.c` supplies that exact symbol, but delegates to the bounded, explicitly granted `warden_doom_wad_class` in PR #110. It **does not** call stdio or touch any host filesystem; upstream `W_OpenFile`, `W_Read`, `W_CloseFile` are called by the host test, not reimplemented. The test stubs only `M_CheckParm` to return no `-mmap` argument (the real Doom argument parser remains for engine integration).

This is deliberately **stacked on PR #110** (`agent/doom-wad-class-bridge-20261106`); it must be rebased onto a reviewed, merged version of that PR when available. A new self-contained source folder and workflow avoid taking ownership of its bridge file. A failed test means investigate the actual ABI mismatch rather than bypassing the upstream header.

Genuine WAD bytes are represented by a tiny synthetic, noncommercial fixture. A standalone upstream file wrapper linking does NOT mean the entire Doom engine is linked or boots. Doom still requires engine, allocation, graphics, timers, input, real authorized game data and QEMU execution. No FPS or memory claims.

Run the pinned checkout's test with `bash tools/doom-real-wfile-link/run.sh /path/to/upstream/doomgeneric` where the folder is inside a git repository at the exact revision. This is a **host-only integration check**. Independent review is required.
