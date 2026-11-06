# Conservative one-vCPU / 512 MiB QEMU argv checker

Task [#212](https://github.com/badwolfzm/pc/issues/212), for Doom G2 native guest test harness preparation. Accept a JSON array containing the **literal executable argv** (no shell strings or interpolated variables), require exactly one `-smp 1` configuration and `-m 512M` (or equivalent 524288K or bare 512). Reject duplicates, SMP topology >1, alternate RAM devices/options, and every unknown option. This intentionally allows a very narrow QEMU command subset. For example, `-machine pc` and `-device` currently reject until individually safe forms are audited, so **do not deploy it unmodified as a general QEMU launcher validator**.

```sh
python3 -m unittest discover -s tools/qemu-onecore-512-argv -p 'test_*.py' -v
echo '["qemu-system-x86_64","-smp","1","-m","512M","-nographic","-kernel","warden-kernel.mb32"]' > /tmp/argv.json
python3 tools/qemu-onecore-512-argv/check.py /tmp/argv.json
```

**Scope limitation:** An argv check cannot prove the process **actually used** that command, booted Warden/Doom, stayed within resident memory or used only one host CPU core. Feed independently captured argv from a trusted launcher into this checker and attach separate machine telemetry and native guest evidence to acceptance gate #155/#146. The reported shared GPU memory is explicitly unverified. No GPU memory is granted or counted by this tool. It does not check external QEMU environment, host-side accelerators or post-launch configuration changes; reject those separately.

Owned exclusively: `tools/qemu-onecore-512-argv/**` and dedicated CI workflow. No kernel, ABI, Doom adapter, existing validator or loader touched. Independent review required before adoption. Local Python 3 unittest: 3 methods passed; QEMU and actual native game not executed.
