# Opt-in native QEMU execution probe for Warden Doom

Task [#230](https://github.com/badwolfzm/pc/issues/230). Unlike the merged [#223](https://github.com/badwolfzm/pc/pull/223) **data-only** QEMU argv validator, this program actually launches an operator-specified, SHA-256-pinned QEMU executable and a SHA-256-pinned Warden kernel image **when explicitly invoked**. It imports and enforces #223's exact QEMU command argument schema: one guest vCPU (`-smp 1`), `-m 512M`, fixed serial/headless/debug-exit device, and TCG/KVM/WHPX acceleration. QEMU binary/kernel paths must already exist. It never invokes a shell.

It imposes a 1–600 second operator-set timeout (default 120), caps serial output to 1 MiB, kills on timeout/overflow, and writes the bounded log and JSON evidence report, including original binary hashes and raw process exit code. **It does not parse guest serial logs or claim the guest booted; the report deliberately says `doom_executed_verified:false`.** Host QEMU can spawn extra host threads despite one guest vCPU. The 512 MiB figure is guest-presented RAM, not a measured peak. It is not a security sandbox and offers no protection against an intentionally malicious QEMU executable even with hash pinning: use a trusted installation and trusted local files. Hash checks are not race-free against replacement of binaries by another local actor. Output/log paths must not alias the input files. Keep output directory private.

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
