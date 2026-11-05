# G0 CPU framebuffer/input probe (issue #64)

Self-contained deterministic fixed-size 160×100 ARGB renderer and four-button input step. The core is freestanding C11 and does not allocate memory or call host APIs. The host test is only an executable correctness check; **not Doom, not a Warden native guest and not a measured FPS result**.

Run:

```sh
cd tools/game-probe
make test
make clean
make CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

On 2026-11-05, both commands passed in a Linux container. Final hash for the boundary-coordinate test: `5d53090a22597096`. A replay of 300 scripted horizontal steps is deterministic and returns to the initial screen; vertical movement is clamped.

**Ownership:** only `tools/game-probe/**`. The eventual Warden guest integration should bind this rasterizer to a capability-checked presentation endpoint and bounded input transport. Rendering on a host, however successfully, does **not** validate the kernel/ABI, isolation, device access, or QEMU game execution. See [roadmap PR #63](https://github.com/badwolfzm/pc/pull/63).

**Next:** integration agent should provide (1) a native guest wrapper using the Warden ABI, (2) a securely mediated pixel presentation path, (3) user input replay, and (4) QEMU tests with exact output hashes and repeated exit/teardown. For Doom, source-port integration is separately required.

## Bounded frame + input transport (follow-up)

`transport.c/.h` adds a fixed 160×100 ARGB framebuffer copy, 16×16 tile
hashes to count changed tiles, and a 32-event FIFO for four keyboard directions.
It rejects wrong dimensions/stride/byte count and refuses overflow or malformed
key events. No malloc and no operating-system calls. This is a **host-tested
primitive**, not a kernel-accessible service or actual Doom integration.

Host checks run in Linux container: `make test` with GCC C11 `-O2`,
warnings-as-errors; `make clean && make CFLAGS='-std=c11 -O1 -g -Wall
-Wextra -Werror -Wpedantic -fsanitize=address,undefined
-fno-omit-frame-pointer' test` to run sanitizer tests. Both transport-specific
tests passed locally before publishing. Existing renderer regression must
also be rerun on this precise GitHub head via CI/reviewer.

**Threat-model boundary:** the buffer sizes and coordinates are checked, but
a direct C pointer is still trusted to refer to accessible memory; the eventual
Warden boundary must copy from validated guest address ranges and verify
capabilities/badges, never blindly dereference guest pointers. Tile hashes
are for change detection, **not cryptographic integrity**. Input events need a
kernel-controlled provenance before exposure to an untrusted guest.

Existing Pong native guest (`tests/kernel/pong.sh`) already exercises
QEMU/VGA/input separately; reusing that proven device path is an integration
option, not proof that this new transport runs in Warden.

## Integrated host replay (2026-11-05, exact PR head)

`game_loop.h/.c` composes input queue drain, deterministic game state,
software rendering and validated copy/present into one caller-owned instance.
`test_game_loop.c` independently replays 301 frames on two instances,
compares every frame hash/dirty count, and checks event handling and bounds.

Verified by GitHub Actions [run 42032996931](https://github.com/badwolfzm/pc/actions/runs/42032996931)
on commit `4a99623d38b1042a1183f5e6ded70649627d903f`:

- Optimized GCC C11 `make clean test`: all three test executables PASS.
- GCC ASan+UBSan `make clean test`: all three test executables PASS.
- Integrated replay final hash: `82182d0eb3b82019`.
- This is a host CI test. Warden/QEMU integration and one-vCPU FPS are **not verified**.

The existing Warden Pong guest uses a 320x240 VGA framebuffer behind
capability-checked SYS_PRESENT and SYS_KEY calls. A future guest adaptor can
scale/copy this 160x100 frame into a 320x240 private source buffer and invoke
those syscalls, but that work requires kernel/guest ownership coordination.
Do not write to device memory directly or bypass capability checks.

## Warden native guest adapter (2026-11-05)

`warden_bridge.c/.h` maps Warden's PS/2 set-1 make/break scan codes
(W/A/S/D, E0 arrow keys) to the bounded game queue, expands the 160×100
raster to a **320×240 guest-owned** buffer, and invokes the Pong feature's
`SYS_DISPLAY_INFO` (60) and `SYS_PRESENT` (61) through a callback. No
guest code receives direct framebuffer mapping or device I/O capability.
Host-side regression `test_warden_bridge.c` mocks the syscalls and validates
pixels, output rectangle, press/release handling and refusal propagation.

`warden_guest.c` is a native ring-3 candidate with the existing entrypoint
symbol `pong_main`; it polls `SYS_KEY` (62), displays 60 frames through
the capability-mediated adapter, uses `SYS_TICKS` (63) and `SYS_YIELD`
to target up to 10 FPS, and emits frame/hash telemetry through `SYS_REPORT`
(64). Compile without changing the kernel:

```sh
bash tools/game-probe/build-warden-guest.sh /tmp/g0-guest-build
```

This produces `game_text.bin`, `game_data.bin` and `game.elf` linked
with Warden's existing `guest/pong/start.S` and `guest.ld`. Warden currently
loads `pong_text.bin` and `pong_data.bin` from its own build step, so
these alternative binaries are **not automatically installed or executed**.
The kernel/Pong owner must coordinate a reviewed, opt-in guest selection
mechanism, with capability-scoped input/display acceptance and exact QEMU
runtime checks. Do not overwrite the default Pong app or falsely report
these compile-only binaries as having run inside Warden.

GitHub Actions [run 42034464438](https://github.com/badwolfzm/pc/actions/runs/42034464438)
passed optimized host regressions, ASan/UBSan regressions, and native
freestanding guest build on the exact implementation SHA
`604497e04b73362c1ecb26eea526e57af3b29004`.
No QEMU boot, FPS, memory-pressure or game-security review was done here.
