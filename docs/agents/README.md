# Warden / MatrixOS multi-agent collaboration

This document operationalizes `control/CONTROL.md` and `control/STATUS.md`
for agents using GitHub to exchange work. These files remain the source of
truth for architectural requirements and acceptance results. Changes to them
must be reconciled with their existing control/bookkeeping ownership.

## Task claim protocol

1. Read `control/CONTROL.md`, `control/STATUS.md`, the relevant contract
   and open GitHub issues/PRs before changing code.
2. Select one narrowly bounded task. Record owner (agent/session or human),
   status, base branch/commit, dependencies and **file paths exclusively
   owned** in `docs/agents/claims/` on the branch. Check for competing
   claims and open PRs before touching another component.
3. Make a dedicated branch and a **draft PR**. Never push unfinished work to
   `main` and never merge without user approval.
4. Write a handoff in `docs/agents/handoffs/` with requirements, files,
   commit(s), checks actually executed, known regressions, dependency
   ordering, and next reviewer questions. Mark missing tests **NOT VERIFIED**.
5. Treat results from other agents as untrusted until independently checked.
   For security-boundary changes, ask a different reviewer to inspect
   capability authority, caller-controlled inputs, teardown, quotas, ABI
   drift, and tests that fail before the fix.
6. Control/bookkeeping owners integrate claims and status into their
   designated files. Agents should not silently rewrite those source-of-truth
   documents or consider a draft PR accepted.
7. A dependent PR targets its prerequisite branch and explicitly states that
   dependency. Rebase onto `main` after the prerequisite merges, then rerun
   all acceptance tests.

## Shared engineering constraints

- Warden is the independent kernel; Linux userland is hosted by a
  capability-mediated personality layer. No hidden dependency on the Linux
  kernel for the bare-metal path.
- Escape resistance outranks convenience and performance; preserve W^X,
  strict capability attenuation, memory/IPC budgets, and denial semantics.
- ABI and compiler decisions must follow repository contracts; deterministic
  builds, not `-march=native`.
- Claims of compatibility require named, unmodified executable workloads
  that run **inside Warden/QEMU**, not just on the Linux host.
- Runs on GitHub CI or cloud QEMU do not substitute for the required target
  PC/WSL2 hardware verification.

## Active claims checked on 2026-11-05

| Slice | Agent | Branch/PR | Owned files | Dependency |
| --- | --- | --- | --- | --- |
| Linux network compartments and quotas | ChatGPT session | `feat/linux-network-container-isolation`, PR #24 | `crates/warden-kernel/user/personality.c`, network namespace header, network tests, CI workflow | `main` |
| Linux ABI query fidelity | ChatGPT session | `feat/linux-abi-fidelity`, stacked draft PR | `crates/warden-kernel/src/linux.rs`, `linux/client.c`, `linux/sys.h`, `src/tests.rs`, agent documentation | PR #24 |

Other workers should claim their slices before editing overlapping paths.
This ledger is a snapshot, not a live lock or proof of exclusive ownership.
