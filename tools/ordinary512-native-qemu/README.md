# 512 MiB / one-vCPU **real Warden guest** QEMU baseline

This is the first native guest profile test associated with the ordinary-modern-PC
goal in [PR #134](https://github.com/badwolfzm/pc/pull/134).
It uses **existing Pong**, not Doom or a modern PC game. It is materially
different from offline page-trace simulation and from host-side real Doom
tests. The expected evidence is Warden's actual in-guest syscall/display and
hostile-thread isolation check log, with emulated guest RAM configured at
exactly 512 MiB and QEMU configured with exactly one virtual CPU.

To avoid modifying the kernel/Pong owners' files in Git history, CI checks
out the repository and runs `prepare.py` against its **disposable CI copy**
of `tests/kernel/pong.sh`. The preparation refuses unfamiliar QEMU command
formats, adding `-m 512M -smp 1` only in the CI workspace. A JSON manifest
records the original/modified test script SHA-256 hashes and exact QEMU
flags. A dedicated workflow runs the existing native Warden Pong test and
its authoritative guest serial-log validator, preserving logs and screenshot.

```sh
python3 -m unittest discover -s tools/ordinary512-native-qemu -p 'test_*.py' -v
python3 tools/ordinary512-native-qemu/prepare.py tests/kernel/pong.sh --manifest target/native512-profile.json
timeout 240 bash tests/kernel/pong.sh
python3 tests/game/check_pong_guest.py target/native512-pong-serial.log
```

The test needs GCC/binutils/Rust `x86_64-unknown-none`/QEMU/NASM and is
**not run on the user's PC**. It uses TCG unless a runner has KVM and
therefore is not a fair CPU/GPU performance comparison. QEMU's `-m 512M`
caps guest-presented RAM, but **does not prove** per-component live physical
frame accounting, 512 MiB peak guest resident pages, low swap pressure,
host RSS, a real Doom/Albion boot, or frame rate on the user's machine.
An actual modern-game result must separately implement and validate
all categories in the strict telemetry contract (draft PR #141),
and support a larger user VA range, gameplay input, and licensed assets.

Scope: exclusive `tools/ordinary512-native-qemu/**` + dedicated CI only,
no edits committed to `tests/kernel/pong.sh` or kernel. Draft; independent
review before merge. The Pong guest retains its own number of ring-3 threads
and zero tolerance for failed hostile-display checks; *one guest vCPU* does
not imply a single guest thread.
