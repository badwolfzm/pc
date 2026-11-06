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

## Two real QEMU boot host-memory comparison

The updated PR #165 workflow runs the existing native Warden Pong test **twice**, collecting separate `/proc` observations for each QEMU host process and validating each run's actual guest serial/isolation evidence. It then runs `compare.py` for a summary of min/max/spread of the **two host peak sampled RSS values** and invokes PR #154's canonical guest two-boot comparator. Unlike the earlier withdrawn PR #158, differing end-screen hashes are not treated as failed boots because host-timed key injection can vary the final game state. Raw serial data, guest reports, both host reports and combined summaries are retained. No actual Doom guest, page trace, swap bandwidth or 512 MiB full-guest residency proof is implied.

## Attribution of host overhead and page faults

Each native QEMU run now records Linux `/proc/<pid>/stat` **host** minor/major page-fault deltas and CPU tick deltas, with process identity verified by `(pid, starttime)` to reject PID reuse. `qemu_observed_span_seconds` measures the sampled QEMU process interval, while `launcher_elapsed_seconds_including_build` also includes the Warden Rust compilation and shell setup. Neither is interchangeable with in-guest frame times. These are observed host process counters, not guest page-fault traces or whole-guest physical frame accounting.
