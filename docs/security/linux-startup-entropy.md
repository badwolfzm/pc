# Warden Linux startup entropy — fail-closed interim change

Issue: https://github.com/badwolfzm/pc/issues/36

## Security defect

Earlier guest setup wrote 16 bytes based on the low 8 bits of `k.ticks` and the program-name length, then advertised them in ELF `AT_RANDOM` (auxiliary vector key 25). For a fixed binary there were no more than 256 startup sequences. This must never be treated as cryptographic randomness.

## Interim behavior

The Linux initial auxiliary vector **omits `AT_RANDOM` entirely**. Warden does not claim startup randomness, does not claim libc canaries are secure, and does not silently use the timer as a substitute. `AT_NULL` and the remaining auxv entries are retained.

**Warning:** Some Linux runtimes require `AT_RANDOM`; others fall back to potentially weak seeds. Removing the advertised entry does **not** make such guests secure, and can break executable compatibility. Do not run untrusted third-party binaries or claim security until real seeded entropy and guest startup behavior are verified.

## Acceptance

- Source regression: `python3 -m unittest discover -s tests/security -p 'test_linux_auxv_random.py'`.
- Run `cargo test` and `bash tests/kernel/run.sh`; inspect all guest failures.
- Execute an actual guest reading auxiliary vectors and distinguish absence of key 25 from a zero-filled/fake value.
- Before restoring `AT_RANDOM`, use a cryptographically seeded, reviewed provider that supplies 16 unpredictable bytes per process, with startup failure/refusal when unavailable; validate isolation, reseeding, size bounds and QEMU/target-PC behavior.
- This change does not implement entropy, the time service, or a secure general Linux loader.

## Verification provenance

This submission only inspects the exact committed source and publishes a regression. Runtime, QEMU and target-PC results must be attached by the integration owner; do not mark them passed without running them.
