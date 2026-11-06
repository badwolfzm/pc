# Warden Doom: strict 1-vCPU / 512-MiB QEMU argument contract

Task [#216](https://github.com/badwolfzm/pc/issues/216), supporting [#212](https://github.com/badwolfzm/pc/issues/212). This is a **host-side static argv checker**, not a QEMU launcher, sandbox, kernel policy, or proof of game execution.

The checker deliberately accepts only this **exact fixed profile** (substitute an actual trusted executable and kernel path, and pick `tcg`, `kvm` or `whpx`):

```text
qemu-system-x86_64 -accel tcg -machine pc -smp 1 -m 512M -display none -serial stdio -no-reboot -kernel /tmp/warden-kernel.mb32 -device isa-debug-exit,iobase=0xf4,iosize=0x04
```

Input is a JSON array of 18 individual argument strings, **never shell command text**. Run `python3 tools/doom-qemu-argv-boundary/validate.py args.json` (exit 0 accepted; 1 rejected). Tests: `cd tools/doom-qemu-argv-boundary && python3 -m unittest discover -p 'test_*.py' -v`.

It refuses alternate devices, injected options, duplicate memory flags, 1024M, two vCPUs, and unsupported accelerators. This makes candidate test command configuration auditable; it does **not** authenticate the named QEMU binary or kernel file, verify hardware acceleration availability, enforce host RSS or count guest processes. An attacker who controls the real launcher can ignore this report. Native guest telemetry and two genuine Doom engine QEMU boots are mandatory before claiming the 512 MiB target. Further QEMU options require explicit independent review instead of weakening the allowlist. No Doom, Albion, or security-performance result claimed.
