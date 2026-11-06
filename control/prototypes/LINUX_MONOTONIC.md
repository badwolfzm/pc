# Linux monotonic host adapter — follow-up to issue #27

Scope: **host-side prototype only**, not the Warden kernel, not a Linux guest syscall, and not a source of secure UTC.

## Implementation

- `linux_monotonic_source.c` calls the host Linux `clock_gettime(CLOCK_MONOTONIC)` only.
- Refuses other clock kinds, including REALTIME, even if the higher-level prototype is incorrectly configured to enable realtime authority.
- Checks the output pointer and validates seconds, nanoseconds and the range of conversion to `warden_timespec`.
- Assigns the output only after a successful host call and complete validation.
- The existing `time_service.c` retains nondecreasing checks, overflow checks and capability/authority gating.
- No host-realtime/UTC implementation is provided, deliberately.

## Exact local validation

Compiled a local equivalent of `time_service.c` plus the adapter and test sources with:

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  time_service.c linux_monotonic_source.c test_linux_monotonic_source.c -o test_clock
./test_clock
```

Observed: `Linux monotonic-clock adapter: all assertions passed`. The assertions include refusal for unsupported clock kinds, missing pointer, unauthenticated realtime, denied capability, 1,001 successful monotonic reads, and unchanged output on refusal. These were **local reconstructed sources**, not a clean checkout of the exact GitHub head; the GitHub branch files were published and inspected through the connector. No QEMU, guest ABI, user Windows/WSL2 host or independent security review was performed.

## Remaining security and correctness work

The monotonic adapter uses host Linux CLOCK_MONOTONIC, which is appropriate for measuring elapsed intervals in this host test, but does not prove guest identity, capability authorization, stable mapping, clock continuity across VM snapshot/migration, or isolation between guests. `bool capability_allowed` and any trusted-realtime flag remain placeholders only for a future trusted adapter. An authorized Warden service must derive the permission from a kernel-issued capability and protect the guest copy. There is no authenticated UTC authority, no uncertainty-freshness enforcement, no concurrency safety, and no verified Linux guest clock implementation. Clocks must not be used for security-sensitive certificate or expiry decisions until those gates pass.

Coordination: PR #48 separately owns the predictable Linux startup `AT_RANDOM` fix; this work deliberately does not edit that file. PR #50 remains draft and should be independently reviewed before merge.


## Follow-up regression tests (PR #50 reviewer request)

A reviewer asked for checked-in boundary cases and injected `clock_gettime` failures. These are now separate source files:

```sh
# 49 conversion combinations, state/denial/refusal/overflow paths.
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  time_service.c test_time_service_boundaries.c -o test_boundaries
ASAN_OPTIONS=detect_leaks=0 ./test_boundaries

# Compile the REAL adapter against an injected mock clock_gettime symbol.
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Dclock_gettime=warden_mock_clock_gettime \
  linux_monotonic_source.c test_linux_monotonic_injected.c -o test_clock_faults
ASAN_OPTIONS=detect_leaks=0 ./test_clock_faults
```

**Observed in this agent's Linux container:** both tests passed using local source matching the reviewed implementation and newly committed test logic. The bound-check harness used unsigned 128-bit arithmetic as an independent overflow oracle. The injected test exercises syscall failure, negative seconds, negative nanoseconds, out-of-range nanoseconds, success, and refusal before provider invocation; rejected calls preserve the output buffer. The exact published GitHub tree was not cloned into this container because network DNS for github.com is unavailable; GitHub API write and file-readback confirmed publication. Do not interpret this as running the full published tree or QEMU/kernel suite. Leak detection was disabled for these ASan runs and is not verified here.

Still needed: independent review of the new tests, production trust-boundary design, kernel/guest integration, QEMU coverage, and user-PC verification.
