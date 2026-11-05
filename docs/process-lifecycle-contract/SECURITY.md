# Linux process lifecycle — security contract (design only)

Task: bounded design slice of [#28](https://github.com/badwolfzm/pc/issues/28). **No lifecycle syscalls are implemented by this document.**

## Current observed boundary (main 70bc55e, 2026-11-05)

- `crates/warden-kernel/src/thread.rs`: a Warden thread currently owns an address space, CSpace, Linux Proc state, trap frame, and ABI personality.
- `crates/warden-kernel/src/linux.rs`: Linux syscall mediation uses a kernel-owned IPC buffer and a badge visible to the personality server; native and Linux personalities are distinct.
- `fork`, `clone`, `execve`, genuine signal delivery, and futex behavior are not implemented. Existing permissive signal stubs must not be construed as compatibility.
- The active ABI PR #26 owns Linux query-fidelity changes. Do not edit its kernel files under this task.

## Invariants before any implementation

1. **No privilege amplification.** A child receives only explicitly attenuated capabilities; never copy opaque parent handles as authority without validating provenance and rights. The child must not acquire a stronger personality endpoint, untyped quota or access to another namespace.
2. **Atomic construction.** Reserve all process/thread IDs and bounded metadata, charge every frame/table/capability allocation, validate flags and memory ranges, then publish the new runnable child. Any failure rolls back each reservation and leaves the parent runnable and unchanged.
3. **Address-space isolation.** Neither parent nor child can mutate the other's page tables or writable private pages. Initial implementation should prefer bounded eager copy; copy-on-write requires refcounts, faults, accounting and rollback before enablement. Shared mappings must be deliberate, policy-checked and separately tracked.
4. **Identity is not authority.** PIDs/TIDs are namespace-scoped opaque identifiers with generation/reuse protection. Every wait, signal, reap and cross-task operation checks authorization and current identity; no global PID lookup may bypass capability mediation.
5. **`execve` is transactional.** Build a fresh validated image, stack, auxv, descriptor/inheritance plan and privilege set off to the side; commit them only once all checks succeed. Failure retains the original executable. Never advertise predictable data as `AT_RANDOM` (see #36). Reject unsupported interpreter, relocations, path and ABI combinations.
6. **Fail closed on unsupported flags.** Unknown clone flags, namespace requests, ptrace-like access, uncontrolled shared memory or privilege-changing operations must return a stable error, not silently degrade to a weaker isolation mode. Preserve one ABI personality per task.
7. **Resources are finite.** Define caps for live tasks, zombie records, address-space pages, open descriptors, futex waiters and pending signals. An attacker must not leak resources by repeated spawn/exit or by dying mid-IPC.
8. **Teardown is complete and idempotent.** Remove scheduler entries and wait queues, revoke or release owned caps and frames, cancel pending personality IPC, dispose descriptors and wake/reap dependent tasks. A second teardown must be harmless.
9. **Signals and futexes do not trust guest pointers.** Validate the entire read/write range and alignment before effects. Avoid TOCTOU by copying or pinning input as required. Wakeups cannot cross process/namespace boundaries without explicit sharing policy.
10. **No host ABI compatibility claim without guest evidence.** Native host Linux reference tests do not establish Warden execution or security.

## Proposed phased behavior (requires owner approval)

| Milestone | Minimal supported behavior | Explicit refusal |
| --- | --- | --- |
| P0 | Document flag matrix and limits; deny unsupported operations | all process creation and signal/futex operations until tested |
| P1 | `fork` of single-thread static guest with eager private page copy, unique PID, wait/exit/reap | multi-thread fork; CLONE_VM; file-backed mappings |
| P2 | `execve` replacement of static validated ELF under same constrained identity | dynamic PT_INTERP/PT_DYNAMIC pending trusted loader |
| P3 | Bounded threads and futex wait/wake with shared-address-space ownership rules | unsupported clone flags, robust futex lists, PI without implementation |
| P4 | Checked signal queuing/delivery and interruption semantics | real-time queues and unsupported signal actions |

Do not infer these stages are implemented; each requires code review and named in-guest tests.

## Required acceptance probes

- Parent/child: child writes private memory, parent reads original; both retain only authorized caps; bad clone flags do not spawn a task.
- Failures: exhaust per-task and global memory/task quotas at *each* allocation point; all partial creations roll back without ghost PIDs, pages or descriptor leaks.
- Identity: stale PID after reuse cannot signal or reap an unrelated task; inaccessible process ID is denied.
- `execve`: malicious ELF, W+X section, unsupported dynamic interpreter, bad pointer and deliberately induced allocation failure preserve old image and caps.
- Threading: futex wrong alignment, unmapped/cross-page pointer, waiter limits, wake-after-exit and racing cancellation cannot hang or corrupt the kernel.
- Signals: invalid signal numbers/targets, denied cross-namespace targets, blocked signal semantics and interruption from syscalls are explicitly tested.
- Regression: current isolated HTTP server/client workload, adversarial tests and QEMU suite still pass; separate WSL2 target-PC results recorded.

## Ownership and interfaces

- This PR changes **only** `docs/process-lifecycle-contract/**`. The Linux syscall dispatcher (`linux.rs`), thread scheduler (`thread.rs`), capability model, ELF loader and personality server remain owned by their existing maintainers/PRs.
- #24 networking and #26 Linux ABI changes must be reconciled before process semantics are integrated. #29 loader work and #36 entropy remediation are dependencies for broader compatibility.
- No new syscall number, capability name, permission, or namespace is reserved here. All changes require the lead and ABI owner to approve a versioned contract.

## Definition of done for this design slice

An implementation owner can map each future syscall to a validated authority boundary, explicit no-support behavior, resource limit, rollback path and negative QEMU probe. An independent security reviewer must assess the plan before implementation is merged.
