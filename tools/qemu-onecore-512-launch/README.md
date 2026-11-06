# Validated QEMU constrained launcher — integration test infrastructure

Task [#229](https://github.com/badwolfzm/pc/issues/229). This runner imports the **already merged** [#221](https://github.com/badwolfzm/pc/pull/221) Python QEMU argument preflight, checks the precise argv passed to a subprocess, requires an absolute executable path and an executable regular binary, and launches it with `shell=False` and a bounded timeout. The exit status is propagated; timeout returns 124 from the CLI. It deliberately does not construct shell command strings or expand extra arguments after validation.

Example (requires an existing, trusted local QEMU installation and a Warden kernel image):

```sh
printf '%s\n' '["/usr/bin/qemu-system-x86_64","-smp","1","-m","512M","-nographic","-kernel","/absolute/path/to/warden-kernel.mb32"]' > /tmp/warden-qemu-argv.json
python3 tools/qemu-onecore-512-launch/launch.py /tmp/warden-qemu-argv.json --timeout 120
```

The merged #221 parser intentionally rejects many normally supported QEMU options including `-machine` and `-device`; this launcher **does not bypass that restriction**. An actual Warden boot command requiring additional options must be separately reviewed and allowlisted in the upstream checker before running it through this launcher.

Run self-contained integration tests with `python3 -m unittest discover -s tools/qemu-onecore-512-launch -p 'test_*.py' -v`. Those tests run a **fake QEMU executable** in a temporary directory to prove argument preservation, process-exit handling, refusal of unsafe options and timeout; they are not a native guest boot. The launcher checks the file at validation and cannot guarantee against local attacker-controlled filesystem TOCTOU or a malicious executable at the supplied path. Use a trusted image and executable source.

**No inference:** successful exit of QEMU is not proof of Warden/Doom gameplay, one host CPU core, graphics RAM accounting, measured 512 MiB guest residency or Albion support. A QEMU `-m 512M` setting is configured capacity only. Attach independent frame, input, memory and CPU telemetry from the canonical native evidence gate (#155/#146). This tool does not replace a full host sandbox.

Owner: current authorized contributor, exclusively `tools/qemu-onecore-512-launch/**` and its CI. No kernel/ABI/loader/WAD/graphics changes. Independent review required.
