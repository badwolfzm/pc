# Time service prototype — issue #27

**Status:** independently compilable **host-side prototype**, not integrated into Warden Linux personality or ring-3 guest syscalls. No UTC trust provider or security certification.

This change deliberately adds new files only. It does not modify `crates/warden-kernel/src/linux.rs`, the ABI table, `control/CONTROL.md`, or `control/STATUS.md`. It complements the merged entropy broker contract in PR #33.

## Compile and validate

From `control/prototypes`:

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer time_service.c test_time_service.c -o time_test
ASAN_OPTIONS=detect_leaks=1 ./time_test
```

These commands passed on a local Linux tool container (GCC, ASan, UBSan) using matching local source before publication. An initial test exposed an error-code mismatch for overflow, which was corrected before committing. Tests are **not** QEMU, WSL2/user-PC, or independent security verification.

## Contract

- MONOTONIC: uses a trusted adapter-supplied time source; validates nonnegative seconds and 0 <= nanoseconds < 1e9, rejects 64-bit nanosecond overflow and backwards readings. Failed reads leave output and prior monotonic state unchanged.
- VIRTUAL: advances only through explicit `warden_time_advance_virtual` calls with overflow checks; never claims UTC or wall clock.
- REALTIME: denied by default, enabled *only* if a trusted adapter declares a validated wall-clock authority and uncertainty. The prototype **does not itself authenticate** UTC, its freshness, or its uncertainty. On loss of authority, REALTIME returns UNAVAILABLE instead of fabricating an epoch.
- Unauthorized and unsupported requests do not call the time source or modify output; uncertainty is returned on successful reads only.

## Required next engineering gates

1. Design an unforgeable kernel-attested capability and issuer for realtime authority. The `bool capability_allowed` and `bool trusted` parameters are **not** safe to accept directly from Linux guests.
2. Add explicit time-source freshness/expiry, uncertainty validation, and suspend/restore continuity. Current prototype trusts callers to enforce those rules; REALTIME is not yet a usable security authority.
3. Add thread-safe per-process monotonic state and synchronize with actual clock source, with bounded concurrency and stable process identity.
4. Implement guest-pointer validation and a transactional copy to the caller's address space, including revoked or concurrently unmapped pages.
5. Integrate the Linux syscall ABI only after coordinating ownership with the other agents; preserve native apps and existing game workload.
6. Run QEMU tests, independent review and actual user's Windows/WSL2 verification.

Not suitable for certificate validation, authentication expiry, or other security-sensitive UTC usage yet.
