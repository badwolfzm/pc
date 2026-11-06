# Single-thread Doom-like frame probe (host baseline)

Task [#66](https://github.com/badwolfzm/pc/issues/66). **This is NOT Doom and does NOT execute in Warden yet.** It is a small deterministic fixed-point, 320×200 column renderer designed for a future ring-3 Warden G0 integration. No game engine, WAD files, protected binaries, Linux kernel or network.

`render.c` and `render.h` have no libc dependency, heap allocation, floating point, system calls or threads. The host-only `main.c` runner uses a static 256,000-byte ARGB framebuffer and CLOCK_MONOTONIC; it reports a rolling FNV-1a checksum (canonical byte order). Test only one host process and one rendering thread; a single host thread does **not** prove CPU-affinity pinning or one Warden vCPU.

Run:
```sh
make -C tools/game-frame-probe test
tools/game-frame-probe/game-frame-probe 300
```

**Acceptance and future handoff:** capture the checksum for 1/60/300 frames, build with fixed toolchain, port just `render.c/.h` as a freestanding guest behind a granted framebuffer capability, display its frames via Warden's existing mediated interface, run QEMU `-smp 1` twice with bounded guest RAM, capture actual frame presentation and memory/CPU time. Use PR #63's roadmap for G0 acceptance. A host run is only an algorithmic smoke test and not a game FPS result. When ported, avoid touching graphics/kernel-owner files without explicit handoff; preserve capability checks and bounded queues. Output digest changes require a deliberate golden-reference review.

**Limitations:** Fixed camera and map; 2.5D columns, not a Doom renderer. CPU FPS on GitHub cloud runner, if reported, is not representative of user's PC. No Albion executable tested and no anti-cheat bypass.
