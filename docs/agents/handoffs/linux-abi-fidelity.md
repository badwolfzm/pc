# Handoff: Linux ABI query fidelity

## Why this slice

The existing `gettimeofday(tv,tz)` used argument 1 as its timeval pointer,
which is incorrect (the timeval is argument 0). Some Linux compatibility
queries were no-op successes without initialized output. `getrandom`
previously used a predictable xorshift generator, inappropriate for
security-sensitive Linux callers.

## Changes

- Correct `gettimeofday` argument handling, explicitly initialize timezone.
- Implement bounded `getrlimit` and `prlimit64` output for the implemented
  stack and descriptor limits; reject unsupported resources and modifications.
- Implement a single-CPU `sched_getaffinity` result and 100-Hz
  `clock_getres` result, with user-pointer checking.
- Return ENOSYS rather than predictable entropy from `getrandom`;
  a real entropy service is a **blocking prerequisite** for broad compatibility.
- Add 6 in-kernel QEMU reports from the Linux client, with exact expected
  results in the judge and syscall numbers defined in the guest test ABI.

## Acceptance and outstanding requirements

- [ ] Rust host-compatible unit tests on PR head
- [ ] Boot Warden under QEMU, all in-guest checks pass on PR head
- [ ] Independent adversarial review of user-pointer, rate/budget, and ABI behavior
- [ ] Confirm correctness on target Windows/WSL2 PC
- [ ] Proper cryptographic entropy service, independent trusted clock source,
      and real libc tests (separate future work)

**Security warnings:** The current wall clock reflects partition uptime,
not real Unix epoch time; do not claim accurate system time. Both the
timebase and `getrandom` require dedicated architectural work.
No application compatibility beyond the named guest checks is asserted.

## Review instructions

Diff against `feat/linux-network-container-isolation`. Review the Linux
syscall table in `warden-abi` (classes remain unchanged). Check invalid
pointer, short-buffer, bad resource, and wrong-pid behavior. Ensure there
is no fallback to kernel privileges or host Linux APIs.
