# Working together on Warden / MatrixOS

Read [control/CONTROL.md](control/CONTROL.md),
[control/STATUS.md](control/STATUS.md), and
[control/COORDINATION.md](control/COORDINATION.md) before editing.
Read the relevant module README and contracts before changing its behavior.
When present, also read `docs/agents/README.md` and the claims/handoffs under
`docs/agents/`; that guide is being introduced by PR #26. Check its current
PR state before assuming it is on main. The templates in COORDINATION.md
supplement that guide rather than creating a second ownership ledger.

## Establish current state

- Record the exact base commit. Inspect current main, open PRs and task claims;
  do not start from a historical hash copied from a document.
- CONTROL.md records scope, ownership and decisions; later rulings supersede
  earlier entries. STATUS.md records reported progress. Neither substitutes
  for inspecting the current code and GitHub state.
- If a board entry disagrees with a commit or PR, report both with their
  references. Do not silently rewrite history or mark a reported result as
  independently verified.

## Claim a bounded task

- Use the claim format in COORDINATION.md. Name the agent/session, owned paths,
  dependencies and acceptance checks before implementation.
- Respect existing file ownership, including shared-kernel-file restrictions.
  An absent issue does not make an owned module available. Resolve overlapping
  work through the lead before editing it.
- Use an isolated clone or worktree and a task branch. Keep changes within the
  claim; route cross-owner requests through the existing coordination channel.
- Preserve uploaded baselines. Do not change syscall reservations, rights,
  permission names or security boundaries without the applicable owner/lead
  decision and updated contracts.

## Deliver evidence and a handoff

- Run checks appropriate to the changed behavior. Record commands, commit,
  environment, exit results and relevant counts. Label skipped/unavailable
  checks and distinguish cloud QEMU/TCG from the target PC/WSL2/KVM.
- Keep host-prototype results, native Warden results and Linux-personality
  compatibility results separate. A syscall classification is not an
  implementation, and module tests are not end-to-end application support.
- Ask an independent reviewer to inspect important security changes; record
  unresolved findings. Do not self-certify an unrun test or another agent's
  reported result.
- Follow the existing bookkeeping-to-lead handoff. Only the lead publishes
  branches/PRs under the standing rules, unless the user explicitly authorizes
  a different workflow for the current task. A task claim itself grants no
  permission to write to GitHub. Follow the applicable current merge policy;
  do not infer merge authorization from permission to open a PR.

GitHub documents and handoffs coordinate separate sessions. Do not assume
access to another agent's private chat, local files, or live state.
