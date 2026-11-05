# G0 native Warden guest adapter (stacked on renderer PR #68)

This is a **buildable guest artifact** that adapts `tools/game-frame-probe/render.c` to the existing Warden Pong ring-3 ABI. It is **not yet booted in Warden**, not playable Doom and not Albion Online.

## Build

```sh
bash tools/warden-game-guest/build.sh
```

The script reuses Pong's `start.S` and `guest.ld` and emits `target/warden-game-guest/game.elf`, `game_text.bin`, `game_data.bin`. No kernel source or Pong binary is modified. Requires GCC/binutils on Linux/WSL2 and PR #68's renderer source. This does not run QEMU.

## ABI and memory

- Uses `SYS_DISPLAY_INFO=60` with display capability in slot 1 and checks 320x240.
- Renders 300 deterministic fixed-point frames at **320x200** into a statically allocated **320x240** 32-bit ARGB buffer (307,200 bytes); bottom 40 rows are black. This is essential: Pong's `SYS_PRESENT` validates a full 320x240 source allocation and uses fixed 320-pixel stride.
- Sends the whole 320x240 rectangle with `SYS_PRESENT=61`, checks every result, and yields each frame via syscall 5. Reports count (key 312) and rolling digest (key 313) over `SYS_REPORT=64`, then exits with status 0 through Pong's startup assembly.
- No heap, SDL, OS threads, actual keyboard interaction, audio or WAD. This adapter is **not a Doom engine**. Yielding does not enforce FPS and does not prove one-vCPU timing.
- The kernel's existing native Pong boot currently loads `pong_text.bin` / `pong_data.bin` and its finish oracle expects Pong-specific report keys. Merely building this adapter does **not** switch boot and would not satisfy the Pong test suite.
- The display syscall is only compiled in the existing `pong` feature, and code uses its current capability and fixed frame ABI, not a generalized display service.

## Needed handoff to kernel/Pong owner (#67)

1. Review the exact adapter source and binary size, especially video source address, W^X guest mapping and 320x240 display-buffer bounds.
2. Introduce a **separate** game guest boot/test feature or manifest selection without modifying existing Pong semantics or weakening capability checks. Do not simply overwrite Pong blobs or its expected reports.
3. Run the game adapter twice under QEMU `-smp 1`, `-m 128M` initially (not an achieved low-RAM target), log key 312 = 300 and compare key 313 digest; independently inspect actual VGA framebuffer output and clean exit.
4. Add negative denial/invalid-address tests, deterministic hash golden, and eventual time, memory, and CPU evidence. Two successful boots and measured FPS/peak RAM are required for G0.
5. Review alongside #58 SIMD/graphics and #61/#60 queued Linux death notices; no overlapping file changes before owner handoff.

This branch owns only `tools/warden-game-guest/**` and its new workflow. Build, CI and actual guest execution remain explicitly separate. No performance claims.
