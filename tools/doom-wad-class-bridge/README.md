# Doomgeneric `wad_file_class_t` → Warden bounded stream adapter

Original integration between upstream Doomgeneric's `w_file.h` ABI and the existing `tools/doom-wad-stream` reader. No upstream engine or WAD is vendored. The test-only `w_file.h` mirrors the public shape for host checks; use the real header in a port.

A preauthorized trusted storage owner provides `wad_read_at`, source size and an exact granted basename via `warden_doom_wad_grant("DOOM1.WAD", ctx, read_at, size)`. The callback must authenticate and authorize reads and return 1 **only for a complete read**. A future engine integration must route Doomgeneric's `W_OpenFile` backend to `warden_doom_wad_class`, not the stdio backend.

Only one open handle and one exact basename are supported. No heap, host file/device access, path enumeration or implicit capability grant. Revocation invalidates reads. Invalid/out-of-bounds/short reads return 0. Stream open validates the WAD header and directory extent; entries are checked individually when requested by the existing stream library.

Run `make test freestanding` in this directory (requires adjacent `../doom-wad-stream`). This proves host callback behavior and compilation of an object, **not actual Doom execution or guest security**.

Integration next steps: pin real GPL Doomgeneric source and use its actual `w_file.h`; link engine with allocator/libc/file APIs; security-review storage callback identity/lifetime and capability boundaries; measure Warden/QEMU `-smp 1` RAM, frames, and complete gameplay tests. Do not edit PR #88 display adapter, kernel, or existing storage owners without coordination.

Limitations: static handle address may be reused after close/reopen (ABA); not safe as an untrusted multi-tenant handle service. Single-process trusted engine only; caller must not retain closed handles. No parallel readers, multiple WADs, proprietary assets, or claimed FPS.
