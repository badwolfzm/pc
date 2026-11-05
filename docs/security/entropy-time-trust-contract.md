# Warden guest entropy and time: trust-boundary contract (design only)

Status: **proposal, NOT implemented**. Owner: independent issue #27 design task.
Base: `main` at `3bf3e6bbd3a2a415b1ab498d9859d7ec64bbc46c`.
No modifications to `crates/warden-kernel/src/linux.rs` while PR #26 owns Linux ABI query fidelity.

## 1. Scope and invariants

This contract defines the conditions under which a Linux-personality guest may receive
cryptographic randomness or wall-clock timestamps without widening Warden's native
microkernel authority boundary. The personality server is *untrusted* relative to the
kernel; a guest-supplied buffer, pointer, length, flags, or clock ID is untrusted input.

* Never claim secure entropy from a deterministic counter, fixed seed, uptime, TSC,
  QEMU device model alone, or an unauthenticated guest-controlled source.
* Never mislabel uptime/virtual time as Unix epoch time. If a trustworthy wall-clock
  source is unavailable, fail queries that require real wall time, or explicitly
  expose a separate virtual-clock interface; do not quietly fabricate an epoch.
* All guest memory ranges must be checked using the calling task's address space
  and effective write permissions. Overflow, unmapped, nonwritable, oversized,
  or cross-boundary ranges fail before any guest-memory mutation.
* Separate caller-specific quotas and service-level limits; depletion must not
  allocate in the kernel's hot path, cause an unbounded queue, or leak other
  callers' state.
* Failures must not partially fill buffers unless the ABI explicitly allows a
  short result; error paths must not reveal uninitialized bytes.
* The ability to read a clock does not imply the authority to configure it.

## 2. Entropy provenance and state machine

Proposed service interface (NOT a shipped syscall):

`entropy.read(caller_badge, guest_span, flags) -> byte_count | -errno`

The trusted entropy provider and its boot sequence require a platform-specific
design review **before enabling the API**. A candidate must identify the actual
seed source, health-test policy, minimum initial seed strength, extractor, DRBG
construction, reseed frequency, state protection, fork/process duplication
semantics, and recovery after failed health checks. Merely finding a CPU random
instruction is insufficient evidence of safe initialization.

States:
1. `UNINITIALIZED`: no seeded DRBG, all cryptographic reads denied.
2. `SEEDING`: source health checks and seeding in progress, reads denied.
3. `READY`: initialized per security review; provide bounded output and enforce
   accounting, including reseed policy.
4. `DEGRADED`: source or DRBG validation failure; deny secure output, record
   diagnostic event without exposing internal state.
5. `SHUTDOWN`: wipe provider secret state when supported; no reads.

Transitions into `READY` are guarded by independently reviewed evidence,
not a boot timeout. The initial implementation should return `-ENOSYS`
(or a documented ABI-appropriate failure) for `getrandom` until the provider
passes these gates. In particular, `GRND_NONBLOCK` must not return made-up
bytes; an unseeded provider may report `-EAGAIN` once nonblocking semantics
are implemented. Unsupported flags are rejected, not ignored.

A single global DRBG reused across isolated guests requires strong
cross-guest isolation and careful process duplication rules; prefer per-domain
derived instances only after a reviewed KDF/domain-separation design.
Avoid logging outputs, seed material, or keys.

## 3. Clock taxonomy

| Clock | Meaning | Trust requirement | Failure behavior |
| --- | --- | --- | --- |
| Monotonic | nondecreasing elapsed time since an arbitrary boot origin | reviewed timer source; overflow-safe conversion | reject unavailable timer |
| Virtual | deterministic time controlled by the isolated guest/test harness | explicitly marked virtual | never substitute for realtime |
| Realtime | actual Unix epoch wall time | authenticated/configured trusted provider, validity and rollback policy | fail when not established |

The x86-64 Linux `clock_gettime` ABI clock IDs and data layouts must be
implemented by the Linux ABI owner. This design does **not** reinterpret
existing ABI IDs or change their implementation. Wall-clock adjustments
must be privileged, audited, and monotonic-clock invariant. For every time
conversion, check multiplication/addition overflow and normalize
nanoseconds into `[0, 1_000_000_000)`.

Time may move backwards on an explicitly designated realtime clock after
trusted correction; monotonic must never move backwards within one
clock domain. Virtual time needs a separate advertised capability or
interface so applications cannot confuse simulation time with secure time.

## 4. Bounds, concurrency, and cleanup

Pre-implementation defaults to review (not claimed product limits):
* Cap each read to **4 KiB** per call; larger requests return a documented
  error or are chunked by a bounded service implementation.
* Cap cumulative per-caller output by a configurable token bucket; avoid
  silently treating the example numbers as security guarantees.
* Authenticate caller identity using kernel-issued IPC badges, never user
  supplied PID values. Reused IDs must not inherit old domain quotas or DRBG
  state.
* Validate pointer+length overflow and destination writability before
  allocating/copying; never trust a personality-server supplied pointer
  without mapping it to the correct guest address space.
* Make quota debit and output generation atomic with respect to failure,
  cancellation, revocation, termination and concurrent calls. Define short
  output policy explicitly; no double-spending or dangling pending requests.
* On caller teardown, revoke pending requests and reclaim all memory and
  accounting slots, with bounded work.

## 5. Dependency and integration ownership

1. **Issue #27 (this branch):** propose the trust model and threat-driven
   acceptance matrix only. No kernel or Linux ABI code.
2. **PR #24:** namespace isolation for loopback; cannot be assumed to imply
   entropy process isolation. Shared server memory/pools remain an explicit
   threat until proven otherwise.
3. **PR #26:** preserves fail-closed `getrandom` behavior and owns the
   Linux ABI syscall handlers, virtual time and resource query work.
4. **Follow-up implementation branch:** only after lead/owner handoff,
   source selection/security review and actual QEMU validation.
5. **Independent reviewer:** validates entropy sources, range checks,
   cross-domain state separation, and exhaustion behavior before merge.

Do not claim all-Linux compatibility or a security guarantee from this
document. This document is not authority to modify current file ownership
in `control/CONTROL.md` or update `control/STATUS.md`.

## 6. Acceptance matrix (to become executable tests)

| ID | Setup | Expected result |
| --- | --- | --- |
| E01 | boot with no trusted seed | no successful secure random read |
| E02 | supported nonblocking call before seed | `-EAGAIN` or documented unsupported failure, never bytes |
| E03 | entropy source health test fails after READY | transitions to DEGRADED, no output |
| E04 | invalid flags / zero / overflowing length | precise documented ABI response; no memory fault |
| E05 | guest destination crosses unmapped or RO page | `-EFAULT`, no write to valid prefix |
| E06 | concurrent callers, one exhausts quota | independent unaffected caller, bounded allocations |
| E07 | revoke caller or terminate while pending | no late write, quotas and state reclaimed |
| E08 | guest attempts cross-namespace entropy state access | denied, no shared secret disclosure |
| T01 | no trusted wall clock source | no fabricated Unix epoch result |
| T02 | monotonically advance and inject clock rollback | monotonic time never decreases |
| T03 | overflow conversion and invalid user pointer | error; no kernel memory corruption |
| T04 | virtual clock deliberately rewound | remains explicitly virtual, not reported as trusted realtime |
| T05 | stop/start or kill/restart isolated guest | no inherited stale clock or quota authority |

**Required evidence:** named unmodified test binaries where applicable;
actual host CPU/OS/compiler versions; Warden/QEMU transcript with counts;
resource-exhaustion and rollback traces; independent adversarial review;
separately run host-PC/WSL2 results. Mark any missing gate **NOT VERIFIED**.

## 7. Current verification

This is a **design-only** change. No Rust build, QEMU run, security test or
hardware entropy health test was performed as part of this contract.
