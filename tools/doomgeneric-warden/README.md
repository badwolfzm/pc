# Doomgeneric's Warden native platform adapter (integration prerequisite)

Task: actual Doom source-port groundwork following native G0 PR #80. This is an
**original platform callback implementation**, not the Doom engine. It is
independent of the Linux kernel and uses Warden's existing, capability-mediated
native display syscall ABI (60–63) from the Pong runtime.

## Contract

The upstream [Doomgeneric](https://github.com/ozkl/doomgeneric) engine exposes
DG_Init, DG_DrawFrame, DG_SleepMs, DG_GetTicksMs, DG_GetKey and DG_SetWindowTitle.
This adapter implements exactly those functions, enforcing 320x200 ARGB (no
CMAP256), displayed in the first 200 lines of Warden's 320x240 framebuffer.
The syscall requires a full 320x240 source stride, so the adapter uses a fixed
307,200-byte guest-owned staging buffer. Input uses the polled PS/2 set-1
scancode interface. Timing is limited to the Warden 10ms partition tick.

Compile a future reviewed Doomgeneric port with
`-DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200` and include its actual header,
not this repository's test-only `test/doomgeneric.h`.

```sh
make -C tools/doomgeneric-warden test
make -C tools/doomgeneric-warden freestanding
```

## Security and correctness boundaries

The adapter's private staging buffer and fixed dimensions prevent uncontrolled
surface sizes. The *kernel* remains responsible for validating caller mappings,
display-capability identity, bounds and privilege isolation. We have not
validated the Doom engine's heap footprint, stack, libc, file dependencies or
WAD loading inside Warden.

**This adapter does not boot or execute Doom.** Required next work:
1. Pin an exact GPL-compatible Doomgeneric engine revision and satisfy its
   libc/syscall dependencies while honoring its GPL licensing requirements.
2. Provide legally sourced WAD data through a bounded, reviewed Warden asset
   mechanism; a preflight parser does not grant file access.
3. Build a native engine image in the Warden guest's limited 2 MiB address
   window or explicitly revise that memory model with security review.
4. Add a distinct guest boot mode, full capability grants, real gameplay/input
   test and at least two single-vCPU QEMU runs before claiming Doom playable.
5. Do not infer Albion Online compatibility, GPU acceleration or FPS from
   this callback-only work.

## Agent coordination

Standalone new paths `tools/doomgeneric-warden/**` and a dedicated CI
workflow. No kernel, renderer, Linux ABI or existing PR branches edited.
This is draft for independent review; do not merge automatically.
