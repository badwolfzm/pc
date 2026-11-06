# Linux startup and getrandom entropy guard

Base main at creation: `b2eb17f1c5c0e57a57f6c672c793cc3638d0c2a9`. Replaces obsolete PR #48 because main now includes a newer BusyBox-capable Linux startup path.

## Scope

1. Omit the predictable tick-derived Linux auxiliary-vector `AT_RANDOM` (key 25) while preserving argc, argv (including BusyBox arguments), envp terminator and other auxv entries.
2. Fail closed on guest `getrandom` requests for any nonzero byte count (ENOSYS). Zero-byte requests succeed without writing memory.
3. Remove the unused xorshift per-guest PRNG state. No claimed entropy provider exists in this patch.
4. Add source regression checks and CI QEMU run.

## Consequences and limitations

Some guest libc implementations use `AT_RANDOM` for stack guards and may fail or choose insecure fallbacks when it is absent. Some programs expect `getrandom` to work and may refuse to start. **Failing closed is not a complete secure-Linux solution.** Do not use untrusted Linux binaries until a real cryptographic entropy provider is implemented, seeded, reviewed, and verified in Warden itself.

A stronger eventual contract: before executing an entropy-dependent guest, either supply 16 trusted unpredictable startup bytes (and provide compatible `getrandom`) or reject that guest startup explicitly; never treat absent entropy as cryptographic assurance.

## Test gates

- `python3 -m unittest discover -s tests/security -p 'test_linux_failclosed_entropy.py' -v`
- `cargo test --workspace --exclude warden-kernel`
- `bash tests/kernel/run.sh`
- Real static BusyBox guest exit/status and startup check in QEMU.
- Independent reviewer checks auxv formatting, zero-length semantics and libc fallback risk.
- Target host WSL2 is an independent verification gate.

GitHub Actions results are CI evidence only, not host PC tests or independent review.
