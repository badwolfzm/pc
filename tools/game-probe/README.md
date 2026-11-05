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
