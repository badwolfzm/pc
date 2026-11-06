# Native Warden guest adapter — integration handoff

This adapter compiles the identical fixed-point renderer from `../render.c` into a standalone freestanding x86-64 Warden guest using the **existing** `--features pong` syscall contract (60–64). No new syscalls, kernel code or privileged host access. Its 320×240 static 307,200-byte buffer matches Warden's existing display stride, while only 320×200 pixels are rendered and submitted through the capability-checked `SYS_PRESENT`.

Build the isolated ELF/flat blobs:
```sh
bash tools/game-frame-probe/guest/build.sh /tmp/warden-game-probe
```

**This does not automatically replace Pong or run the guest inside QEMU.** To integrate, kernel/graphics owners must explicitly claim and patch the Pong feature's `include_bytes!` artifacts and expected report checks or add a separately authorized app selection. Avoid blindly replacing Pong: it is an independent existing regression suite. Kernel already reserves guest code 0x800000 and data 0x840000, maps guest data through 0x9e0000 and stack through 0xa00000. The renderer references no libc, malloc, floating point or POSIX state.

The native adapter executes 60 fixed frames and reports 40=frames, 41=rolling digest over those frames. Host reference for 60 frames must be compared with a real in-guest report before declaring G0. A QEMU run must show actual display, capability-granted presentation, denial tests and exit with `-smp 1`, twice, with memory evidence. Host toolchain and guest linker conformance is **compile-only** evidence; no native guest execution has occurred.

Dependencies: graphics owner PR #58; kernel acceptance issue #67; IPC issue #61. Review `SYS_PRESENT` source bounds and guest resource accounting before enabling. No Albion Online or Doom engine is involved.
