# Genuine Doomgeneric WAD dispatch integration contract (host-only)

Purpose: prove that **actual upstream** `w_file.c` calls the bounded Warden WAD class in the ordinary non-mmap mode. The upstream source at pinned commit `e257a09527062bfb986b6e5e0780839b6cbd5b71` has an explicit `stdc_wad_file.OpenFile` call; merely defining `warden_doom_wad_class` in PR #110 does **not** reroute this call.

The host test intentionally supplies the exact `stdc_wad_file` symbol as a thin forwarding adapter, while **omitting** upstream `w_file_stdc.c` from the link. Both providers must never define the symbol simultaneously. It compiles upstream's real `w_file.c`, real `w_file.h`, and Warden's existing stream and class bridge; verifies explicit basename grant, successful read through upstream `W_Read`, second-open rejection, out-of-range and provider failure, revoke and closed access. This profile supports **one live WAD only** and has a documented stale-handle ABA limitation.

```sh
git clone https://github.com/ozkl/doomgeneric /tmp/doomgeneric
git -C /tmp/doomgeneric checkout --detach e257a09527062bfb986b6e5e0780839b6cbd5b71
bash tests/doom-wad-upstream-dispatch/run.sh /tmp/doomgeneric/doomgeneric
```

This is **host-only** WAD dispatch integration evidence; it does NOT link the Doom engine, boot Warden/QEMU, establish security of the eventual storage provider, or measure FPS or RAM. The game owner must still implement the full libc/allocator, asset lifetime and authorization, boot wiring, original Doom frame/gameplay execution, two one-vCPU guest runs, and license compliance.

Dependencies: merged `tools/doom-wad-stream` plus **draft PR #110** `tools/doom-wad-class-bridge`. This branch is based on a pinned PR #110 snapshot if necessary; merge only once PR #110 is integrated and CI rerun against exact resulting main. No edits to the bridge, kernel, graphics, shared contracts or Doom engine upstream.
