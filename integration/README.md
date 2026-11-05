# Warden / MatrixOS single-commit integration staging

This directory is a **non-merging coordination and verification package**, prepared on 2026-11-05 at main `8174e7d0d2af5aa07a14a9c8902736b917c61979`. It does not copy other agents' unpublished work and does not claim their patches are approved. The eventual consolidated code change is NOT prepared or tested by this documentation-only commit.

## Authority and boundaries

- Follow `AGENTS.md`, `control/CONTROL.md`, `control/STATUS.md`, and `control/COORDINATION.md`.
- User explicitly authorized this preparation in the current chat, bypassing the normal lead-only publication restriction for this scoped preparation. This does **not** authorize merging other PRs or changing their ownership.
- Exclusive new paths: `integration/**`. Do not edit `control/**`, `docs/agents/**`, `crates/**`, `matrixos/**` or another agent's branch.
- This PR is a draft handoff, not a replacement for other active PRs.

## Observed open PRs (snapshot; always re-query live GitHub before integration)

| PR | Branch / base | Role | Constraint |
| --- | --- | --- | --- |
| [#24](https://github.com/badwolfzm/pc/pull/24) | `feat/linux-network-container-isolation` / main | network compartments | prerequisite for #26; QEMU cross-namespace test pending |
| [#26](https://github.com/badwolfzm/pc/pull/26) | `feat/linux-abi-fidelity` / #24 branch | ABI queries, fail-closed entropy | stacked; do not apply without #24 and re-review touched kernel tests |
| [#31](https://github.com/badwolfzm/pc/pull/31) | `design/warden-entropy-time-trust-contract` / main | entropy/time trust design | reconcile with #33, do not blindly accept both as authoritative |
| [#33](https://github.com/badwolfzm/pc/pull/33) | `agent/entropy-time-contract-27` / main | entropy/time contract design | reconcile with #31 and the control owner |
| [#34](https://github.com/badwolfzm/pc/pull/34) | `feat/elf-compat-preflight` / main | read-only ELF preflight | verify against actual Warden loader; preflight is not execution |

Issue [#35](https://github.com/badwolfzm/pc/issues/35) claims `tools/linux-abi-reference/**` and must not be appropriated by this package. Issues [#27](https://github.com/badwolfzm/pc/issues/27), [#28](https://github.com/badwolfzm/pc/issues/28), [#29](https://github.com/badwolfzm/pc/issues/29) cover wider unresolved runtime work; no completion is implied.

## One-big-commit acceptance protocol

1. Freeze a new, current main SHA and approved source PR SHAs. Re-read ownership claims, all changed-file lists, CI and independent reviews.
2. Obtain independent security review and explicit owner/lead resolution of overlaps. Choose *one reconciled* entropy/time contract; record rejected/retained clauses. Resolve #24 -> #26 stacking.
3. Create a fresh isolated integration worktree from that frozen main SHA. Apply only accepted changes in dependency order, resolving each conflict explicitly against the current source and contract. Never use a forced ref update.
4. Run all applicable host Rust tests, escape/compat suites, kernel/QEMU checks and targeted ABI/network/ELF negative tests. Capture commands, environment, exit codes, counts and artifacts. Run the intended games or binary workloads before claiming compatibility. WSL2/PC verification is a separate gate.
5. Check `git diff --check`, changed files, permissions/ABI changes and binary/symlink safety. Obtain independent review of the *actual combined diff*.
6. Only after all gates pass, create **one code commit** (not a series of per-file GitHub contents API commits) and open a review PR. Preserve original PR references and authorship in the commit/PR description; do not claim it is Git-author-preserving if squash metadata is lost.
7. Merge only with explicit user approval and current head verification. Keep drafts and unresolved issues unmerged.

## Required evidence record for eventual code commit

Copy `integration/EVIDENCE_TEMPLATE.md` into that integration PR and fill in every result. Unknown tests are BLOCKED/NOT RUN, not PASS.
