# Observe real native Warden QEMU HOST RSS (one vCPU / 512 MiB guest option)

This CI extension uses merged #154 to boot real Warden Pong with QEMU
`-m 512M -smp 1`, observes the actual QEMU subprocess via Linux `/proc`,
and records its sampled host `VmRSS`, kernel `VmHWM`, and thread count.
The existing native Pong test and its 15 kernel isolation checks still
decide guest-test success. CI preserves host data and guest kernel evidence.

This is intentionally different from merged #156's generic Linux **host workload**
RSS sampler, offline page models, and future full Warden guest resident-frame
instrumentation (#153). Only a fixed known repo Pong script is executed,
after PR #154's guarded temporary CI profile conversion. No game downloads
or untrusted arbitrary command execution.

```sh
python3 -m unittest discover -s tools/ordinary512-qemu-host-rss -p 'test_*.py' -v
python3 tools/ordinary512-native-qemu/prepare.py tests/kernel/pong.sh --manifest target/native512-profile.json
python3 tools/ordinary512-qemu-host-rss/measure.py --manifest target/native512-profile.json --log target/qemu-serial.log --report target/qemu-host.json
python3 tests/game/check_pong_guest.py target/qemu-serial.log
```

**Caution:** host QEMU RSS/HWM includes emulator overhead and does not equal
the guest's physical frame use, actual virtual-memory working set, or 512 MiB
total guest residency. `-m 512M` is an advertised guest RAM option.
Host process polling can miss short spikes, though VmHWM helps. The QEMU
host may use multiple helper threads even with only one guest vCPU.
This is Pong, not Doom/Albion/modern game FPS evidence; it does not impose
a host RSS cap or provide an isolation security boundary.

Owned paths: `tools/ordinary512-qemu-host-rss/**` and its dedicated workflow.
Independent security/integration review is required before merging.
