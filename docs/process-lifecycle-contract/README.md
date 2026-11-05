# Lifecycle ABI gap audit (source-level, not Warden execution)

Run from the repository root with Python 3.9+:

```sh
python3 docs/process-lifecycle-contract/audit.py
python3 -m unittest discover -s docs/process-lifecycle-contract -p 'test_*.py' -v
python3 docs/process-lifecycle-contract/audit.py --assert-no-silent-success
```

The last command deliberately returns exit **1** while any audited pass-tier syscall has a literal unconditional `ret(Ok(0))` arm. Exit 0 means no such pattern was found, **not** that the syscalls behave correctly or are secure. Exit 2 indicates a setup/parser failure.

This utility inventories only known process, signal and futex syscall names in the versioned ABI registry. It parses literal one-line `pass()` match arms conservatively, reports `silent_success`, `explicit_pass_arm`, `no_explicit_pass_arm`, or `outside_pass_dispatch`, and checks missing registry entries. It does not interpret Rust or prove that dynamic dispatch will select a branch. `no_explicit_pass_arm` and `outside_pass_dispatch` are *unknown behavior*, not proven ENOSYS/EPERM. Source formatting or dispatcher changes may require updating the parser and reviewing the result.

The reporting mode uses no external packages; its tests use synthetic dispatchers and deliberately mutate the silent-success arm. The script never modifies kernel source. Do not run assertion mode as a required CI gate until the security/ABI owner explicitly resolves existing compatibility stubs and approves the intended fail-closed behavior.

## Evidence (this agent session)

- `python3 -m unittest -v test_audit.py` in a local Python container: 6 passed, including mutation, missing-arm, malformed-source and assertion-exit checks.
- Published contents require GitHub readback verification.
- Running against the complete checked-out repository, Rust compilation, real QEMU guest probes, PC/WSL2 and independent review: **not performed**.
