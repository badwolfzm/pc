# Opt-in native QEMU execution probe for Warden Doom

Task [#230](https://github.com/badwolfzm/pc/issues/230). Unlike the merged [#223](https://github.com/badwolfzm/pc/pull/223) **data-only** QEMU argv validator, this program actually launches an operator-specified, SHA-256-pinned QEMU executable and a SHA-256-pinned Warden kernel image **when explicitly invoked**. It imports and enforces #223's exact QEMU command argument schema: one guest vCPU (`-smp 1`), `-m 512M`, fixed serial/headless/debug-exit device, and TCG/KVM/WHPX acceleration. QEMU binary/kernel paths must already exist. It never invokes a shell.

It imposes a 1–600 second operator-set timeout (default 120), caps serial output to 1 MiB, kills on timeout/overflow, and writes the bounded log and JSON evidence report, including original binary hashes and raw process exit code. **Warden uses QEMU's `isa-debug-exit` encoding:** a guest value 0 yields raw QEMU exit status 1 (accepted as an expected debug exit), a guest value 1 yields raw status 3 (failure), and a bare host status 0 is not accepted as proof of the expected guest debug exit. The report retains both the raw return code and decoded guest debug-exit value; it still does not prove Doom started or ran. **It does not parse guest serial logs or claim the guest booted; the report deliberately says `doom_executed_verified:false`.** Host QEMU can spawn extra host threads despite one guest vCPU. The 512 MiB figure is guest-presented RAM, not a measured peak. It is not a security sandbox and offers no protection against an intentionally malicious QEMU executable even with hash pinning: use a trusted installation and trusted local files. Hash checks are not race-free against replacement of binaries by another local actor. Output/log paths must not alias the input files. Keep output directory private.

Example on Linux with the actual host binaries/paths and their operator-verified SHA-256 checksums:

```sh
python3 tools/doom-controlled-runner/run.py \
  --qemu /usr/bin/qemu-system-x86_64 --qemu-sha256 YOUR_64_CHARACTER_QEMU_SHA256 \
  --kernel /path/to/warden-kernel.mb32 --kernel-sha256 YOUR_64_CHARACTER_KERNEL_SHA256 \
  --accel tcg --timeout 120 \
  --log /tmp/warden-serial.log --report /tmp/warden-run.json
```

Unit tests: `python3 -m unittest discover -s tools/doom-controlled-runner -p 'test_*.py' -v`. The tests exercise subprocess lifecycle using Python as a harmless fixture; they do **not** run QEMU or a game.

Limitations: the output file paths may still be subject to symlink TOCTOU from other local users; do not run in hostile shared directories or as elevated/root. Future work needs authentic two-boot QEMU evidence, actual upstream Doom ring-3 ELF and frame telemetry, guest memory high-water measurement and independent review before production adoption. No kernel, game engine, graphics or loader files changed.


## Exact-head real QEMU launcher smoke (standalone fixture only)

The dedicated workflow now builds `smoke-fixture.S` with GNU `as --32` and `ld -m elf_i386` using `smoke-fixture.ld`. This self-contained Multiboot V1 guest writes the fixed `WARDEN_SMOKE` marker to COM1 and exits using the `isa-debug-exit` port with guest value 0. The smoke script computes SHA-256 for the **actual installed QEMU executable** and generated Multiboot fixture, then calls the real `run.py` launcher **twice**, using separate output paths, `-smp 1`, `-m 512M` and TCG. Both logs must contain the marker, match their own report digests, and report expected raw QEMU exit status 1 with no timeout/overflow. Both reports explicitly retain `guest_boot_verified:false` and `doom_executed_verified:false`.

[GitHub Actions run 42279884720](https://github.com/badwolfzm/pc/actions/runs/42279884720) **SUCCESS** on code/CI commit `65701f6774e7ff83bbb3caf61404c29f5d6b1216`: Python unit/CLI fixture tests, package install, and both real-QEMU Multiboot smoke boots completed successfully. This validates **the actual QEMU invocation path**, not the real Warden kernel, actual Doom engine startup, input, graphics frames, game FPS, or genuine in-guest memory residency. The Multiboot fixture is custom standalone assembly that does **not** contain Warden or Doom source. No Warden-native game boot is claimed.

Run on an appropriately provisioned Linux machine:

```sh
python3 tools/doom-controlled-runner/smoke_real_qemu.py
```

This test needs `qemu-system-x86_64`, GNU `as` and `ld`; an executable QEMU must be trusted. Test-only output is generated under a temporary private directory and removed after completion. Native Doom integration still requires a separately verified real engine ELF, and authentic two-boot serial/frame/memory evidence.
