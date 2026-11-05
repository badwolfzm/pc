# Warden Linux guest entropy and time services — draft v0.1

Status: **DESIGN ONLY; NOT IMPLEMENTED OR SECURITY-VERIFIED**.
Issue: [#27](https://github.com/badwolfzm/pc/issues/27).
Dependency: [PR #26](https://github.com/badwolfzm/pc/pull/26) (query-fidelity and fail-closed `getrandom`).
Owner of this document only: issue #27 agent. This proposal does **not** authorize edits to `crates/warden-kernel/src/linux.rs` while #26 owns that file.

## A. Non-negotiable invariants

1. Warden remains an independent capability microkernel. Linux emulation is a *user-space personality service*, not an embedded Linux kernel.
2. **No fake secrets:** neither partition uptime nor a deterministic seed, CPU cycle counter, unseeded PRNG, host clock, guest-supplied bytes, or test fixtures may be used as production cryptographic entropy. On absent/failed trust, `getrandom` fails closed. Returning zeroes or success with predictable output is forbidden.
3. **No fake epoch:** uptime/virtual time must never be presented as UTC/Unix epoch. A wall-clock query either uses an authenticated or otherwise explicitly trusted time source with defined freshness/uncertainty or returns an unsupported/unavailable result.
4. Every request is checked for guest-pointer bounds, overflow, allowed guest memory access, declared capability, length and per-caller quota **before** allocating, copying or advancing a random stream. In particular, a refused request must not consume quota or expose partial random output.
5. Secret state is per service domain and must not be mapped into guests, logged, included in panic reports or copied into untrusted snapshots.
6. A service reset or cloning a VM image must not silently replay the same cryptographic stream. Without fresh verified reseeding and rollback resistance where required, cryptographic requests refuse until trust is re-established.
7. Security, failure behavior, and resource limits take priority over Linux ABI coverage and latency.

## B. Trust and capability contract

The trusted boundary is a minimal entropy/time broker with narrowly delegated capabilities. It cannot acquire arbitrary guest capabilities, grant more memory than its budget, or bypass the kernel's mapping checks. The Linux personality service receives only distinct `entropy.read`, `clock.monotonic`, and optionally `clock.realtime` grants; the exact permission names require a reviewed additive ABI revision (do not assume they exist in `abi/warden-abi.json`). The broker must authenticate its provider and provenance via a trusted platform or host boundary, *not* through an arbitrary Linux guest request.

**Entropy state machine:**

| State | Inputs | `getrandom` behavior |
|---|---|---|
| UNINITIALIZED | No seed or no validated source | Fail closed; do not mutate guest destination |
| READY | OS/platform CSPRNG-seeded with successful provider checks | Serve bounded bytes via a reviewed CSPRNG with per-domain separation |
| DEGRADED | Health check, reseed, suspend/restore, provenance, or rollback check fails | Fail closed until successful reinitialization |
| SHUTDOWN | Broker terminated | Fail closed; zeroize best-effort secret buffers |

Production provider requirements: an approved operating-system CSPRNG or hardware-backed, independently conditioned seed source with documented authentication, sufficient initial seed strength (target at least 256 bits), domain separation, fork/clone detection, reseed strategy, error reporting and health tests. CPUID/RDRAND/RDSEED availability *alone* is not proof of sufficient entropy. Do not mix untrusted bytes and then describe them as entropy. For VM snapshot restore, require a trusted post-restore reseed/uniqueness signal; if unavailable stay DEGRADED. Health checks cannot prove unpredictability, so they do not replace source provenance.

Linux `getrandom` flags: explicitly accept only individually implemented ABI-defined bits, reject unknown combinations with `EINVAL`. Zero-length requests return zero only after valid flags validation; do not touch the guest pointer. Define and test Linux-style `GRND_NONBLOCK` behavior for an unready provider; never spin or block inside an unbounded kernel critical section. For large requests either reject above the documented per-call limit or chunk with a specified partial-result contract; the first slice should enforce a small bounded maximum and return a documented error. The exact errno, behavior during startup and partial-copy policy must be reviewed against the Linux ABI before claiming compatibility.

Buffer permissions: require **writable** guest mappings for the whole requested range; checked addition (`start + len`) must not wrap; reject crossing into kernel or another process. Avoid time-of-check/time-of-use issues: pin the authorized destination mapping or perform access through a validated bounded copy primitive with stable ownership. Check and commit quota atomically per caller; rate limit requests and bytes separately. In a failure, no guest bytes are modified. **Do not copy secrets into the caller until every applicable check passes.**

## C. Three clocks, not one

| Clock | Semantics | Source and invariants | Failure |
|---|---|---|---|
| MONOTONIC | Nondecreasing interval/timeout measurement | Trusted monotonic counter + wrap-safe conversion; calibrated frequency; no wall-clock claims; maintain continuity across migration or explicitly refuse | Error if trustworthy continuity unavailable |
| REALTIME | UTC Unix epoch nanoseconds with explicit uncertainty | Authenticated/explicitly trusted wall clock; bounded uncertainty and freshness; policy for time jumps; no uptime substitution | Error if trust absent; never fabricate UTC |
| VIRTUAL | Guest-visible simulation/partition time | Monotonic virtual scheduler state with specified pause, speed and reset behavior | Mark as virtual; not valid for certificate verification or UTC |

All time representations use checked signed integer conversions and defined saturation/rejection policy. Treat `timespec` nanoseconds as `0 <= tv_nsec < 1_000_000_000`; reject overflow and malformed inputs. Separate `clock_gettime`, `gettimeofday`, `clock_getres` and any timer/timeout interfaces by ID and documented mapping. `CLOCK_MONOTONIC` must not run backwards per guest even if the provider adjusts; `CLOCK_REALTIME` may change after trusted synchronization, but consumers must be told whether it is available and its uncertainty. UTC must not be inferred from TSC alone.

**Policy for current prototype:** preserve existing virtual clock behavior only if labeled accurately; real epoch time and cryptographic randomness remain unavailable until their trust gates pass. Never silently relabel old behavior as a Linux-compatible realtime clock.

## D. Bounded resource and isolation model

- The broker owns a fixed-capacity request queue and bounded memory arenas. Queue-full responses fail promptly, without growth or priority inversion.
- Each guest/process identity is derived from a kernel-attested sender badge, **not** a self-asserted PID. Enforce byte and request budgets per badge and a global ceiling; avoid cross-guest denial of service.
- No shared read/write CSPRNG state between security domains without explicit cryptographic domain separation. Rekey before reuse after snapshot, PID/identity reuse, crash or process restart.
- Timing and status errors must not reveal another guest's random output or sensitive state. Logs contain counts and reason codes, never seeds, keys, output or raw guest memory.
- Treat untrusted clocks as *data* and not an authority for authentication, signed updates, expirations, or security audit timestamps.

## E. Test matrix and acceptance gates (not yet run)

| ID | Test | Required outcome |
|---|---|---|
| E01 | Fresh boot without trusted seed | `getrandom` refuses; destination bytes unchanged |
| E02 | Refused invalid/unknown flags, including length zero | `EINVAL`; no provider state or quota change |
| E03 | Out-of-range and wrapping destination pointer | Refusal before allocation, copy or CSPRNG advancement |
| E04 | Read-only, NX, unmapped, foreign address and revoked mapping | No unauthorized guest write; no kernel fault or secret leak |
| E05 | Max call size + 1, quota exhaustion and queue saturation | Deterministic bounded failure; no unbounded allocation |
| E06 | Two guest badges requesting bytes concurrently | Stable isolation, accounting and domain-separation guarantees |
| E07 | Source failure, bad reseed, cloned state and VM snapshot restore | DEGRADED then fail closed until a *fresh trusted* reseed |
| E08 | Seeded service smoke test | Distinct outputs; this is **not** a proof of unpredictability |
| T01 | Monotonic repeated reads, counter wrap and simulated backwards source | Nondecreasing values or explicit failure |
| T02 | Boot without realtime authority | No made-up Unix epoch; realtime query refuses |
| T03 | Verified wall-clock provider, bad/stale provider and clock adjustment | Correct UTC mapping and visible failure/uncertainty policy |
| T04 | Timespec boundaries and signed overflow | Correct validation and no memory corruption |
| I01 | Existing two-app HTTP workload and native Warden app | No regression to isolation or existing workload |

**Mandatory evidence to close issue #27:**
1. Independent security review of provenance, rollback handling, quotas, TOCTOU/mapping safety, and isolation; review comments resolved.
2. Exact implemented ABI contract, including errnos, maximum sizes and clocks, with named unmodified Linux guest probes where applicable.
3. Host unit tests with a deliberately **non-production** deterministic test provider clearly isolated from release builds; injectable fail/restore cases.
4. QEMU in-guest results, logging named test IDs, commit SHA and result counts; test boot both unseeded and trusted-seeded configurations.
5. Separate measurements on the user's Windows/WSL2 host. A cloud QEMU pass is *not* host-PC verification.
6. No "secure", "cryptographically random", "Unix time", or "Linux-compatible" claim until the respective gate is met.

## F. Integration and agent handoff

Work sequence: (1) review this contract alongside PR #26; (2) choose a platform-backed, verifiable seed and time source; (3) implement a small independently reviewed broker behind the capability boundary, with all failure tests; (4) obtain `linux.rs` file ownership after PR #26 lands, then wire syscalls; (5) run QEMU, regression tests and host-PC checks.

This document deliberately does **not** implement the provider, kernel ABI, Linux syscalls, QEMU tests or a verified source of UTC. Those remain unresolved work. Record updates through the existing bookkeeping/lead protocol in `control/STATUS.md` and `control/CONTROL.md`; this task does not edit those files.
