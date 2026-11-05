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
