# Entropy broker prototype (issue #27)

This directory is an **isolated C test prototype**, not linked into Warden, not a cryptographic generator and not production-ready. It is deliberately outside the agent-owned kernel/ABI files. Its provider callback may be attached only after a trusted integration layer establishes entropy provenance. The example provider in the test is **deterministic and insecure**, and is exclusively for testing failure and accounting.

## Host test

From this directory:

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  entropy_broker.c test_entropy_broker.c -o test_entropy_broker
ASAN_OPTIONS=detect_leaks=1 ./test_entropy_broker
```

These checks were executed locally on a Linux tool container with C compiler and ASan/UBSan against an equivalent local implementation, and **passed**. The GitHub-published files were not fetched and compiled from this commit in QEMU, WSL2, or on the user's PC. Do not describe this as a Warden kernel test.

## What is implemented

- fixed 256-byte per-call bound; 8 concurrent badge slots; 1,024-byte and 16-call per-badge limits (compile-time test configuration)
- no heap allocation in the request path
- fail closed when uninitialized, degraded, or the provider reports failure
- no caller-visible buffer mutation on a refused request or provider failure
- provider execution only after authorization and quota checks
- no quota consumption on a refused or failed call
- best-effort transient buffer wiping (not a production zeroization guarantee)

## Critical integration limitations

1. `trusted_ready`, `capability_allowed` and `output_validated` are booleans supplied by the **caller**. An untrusted guest must NEVER call this API directly. A trusted adapter must derive identity from an authenticated kernel badge, verify its capability and pin/check the full writable guest buffer. This prototype does not verify guest pointers or prevent TOCTOU.
2. There is no real entropy provider, health-test verification, entropy accounting, seeded CSPRNG, rollback/snapshot detection or secure key-state zeroization. A caller passing `trusted_ready=true` is **not** a proof of cryptographic quality.
3. No thread safety, lock/atomic protocol, fairness, time-bound quotas, or process lifetime recycling. A real service needs concurrency-safe accounting, revocation handling, reset and rekey, service quotas and an explicit per-domain separation design.
4. This broker cannot guarantee all-or-nothing copies to arbitrary guest memory: an eventual integration must pin and validate the destination. This prototype's guarantee applies only to the validated ordinary memory buffer supplied to the C function.
5. Linux `getrandom` ABI details/flags/errnos, real-time and monotonic clocks, `AT_RANDOM` at process startup, and QEMU guest tests remain unimplemented.
6. **Existing code audit alert:** `crates/warden-kernel/src/linux.rs` constructs `AT_RANDOM` from ticks and executable name length. This is predictable and MUST be replaced using an approved source or make launch fail closed before claiming safe use of libc/cryptographic processes. Coordinate that fix with PR #26's file owner; do not edit it concurrently.
7. No host-PC or WSL2 verification. Independent security review required.

For the complete contract and gates, see [../contracts/entropy-time-services.md](../contracts/entropy-time-services.md).


## Linux host provider slice (not connected to Warden)

`linux_entropy_provider.{h,c}` delegates to the **host Linux kernel's getrandom(2) CSPRNG** using `GRND_NONBLOCK`. It refuses when unavailable instead of generating predictable output. Provider output is staged and copied only after a complete successful read; transient bytes are wiped best-effort. A maximum of 16 getrandom attempts prevents an unbounded EINTR/partial-read loop. `warden_linux_entropy_attach` probes a 32-byte read before enabling the broker and fails closed on unsuccessful probe. The trust assumption is a non-compromised, correctly seeded Linux host kernel; neither the probe nor a differing-output smoke test independently proves cryptographic unpredictability.

### Provider host test

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  entropy_broker.c linux_entropy_provider.c test_linux_entropy_provider.c -o provider_test
ASAN_OPTIONS=detect_leaks=1 ./provider_test
```

**Observed:** equivalent local copies of the broker and published provider/test source compiled and passed on the tool container with GCC, ASan and UBSan. The full published GitHub checkout itself was not compiled due to unavailable direct raw-GitHub network access. It has not been run on the user's PC or in Warden/QEMU.

**Not done:** a secure host-to-guest trust bridge; authenticated kernel identity/capability checks; stable pinned guest writable pages; concurrent synchronized quota accounting; Linux syscall/ELF `AT_RANDOM` integration; suspend/restore protection; and a trusted monotonic/UTC clock. The original C broker API accepts booleans and is **not safe to expose directly to guests**.
