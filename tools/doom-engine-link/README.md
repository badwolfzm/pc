# Genuine pinned Doom whole-engine host linkage (G1)

CI clones `ozkl/doomgeneric` at **e257a09527062bfb986b6e5e0780839b6cbd5b71**. All upstream Doom engine translation units from `SRC_DOOM` are compiled using the merged `doom-upstream-compile` inventory tool. We omit the original SDL/host platform backend `doomgeneric_soso.c` and original stdio WAD backend `w_file_stdc.c`; otherwise the upstream engine remains unchanged. The link includes the real Doom core, Warden Doomgeneric callbacks (PR #88), capability-scoped WAD class (merged PR #110), its upstream `stdc_wad_file` ABI shim (PR #114), and a host smoke main/syscall mock.

`bash tools/doom-engine-link/run.sh /path/to/pinned/doomgeneric` requires the source at the pinned revision. The CI workflow checks exact SHA and brings in specific unmerged adapter commits as read-only dependencies, without editing those branches. No WAD or engine code is copied into this repository. Comply with upstream GPL when distributing actual combined binaries.

**Acceptance when CI succeeds:** one host ELF executable links all genuine upstream Doom engine modules with the Warden WAD/display adapters. It starts a mock-only smoke, not actual Doom. **Not proven:** freestanding/no-libc link, Warden kernel loading, native gameplay frames, asset provenance, 1-vCPU QEMU operation, bounded guest RAM, real FPS, Albion compatibility.

Dependencies: `tools/doom-upstream-compile/` on main; `tools/doom-wad-stream/` on main; `tools/doom-wad-class-bridge/` on main; PR #114 upstream backend link, PR #88 Doomgeneric platform callbacks. Exclusive ownership: `tools/doom-engine-link/**` and dedicated workflow. Review all new requirements before attempting native kernel integration.
