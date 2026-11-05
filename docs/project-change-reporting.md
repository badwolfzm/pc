# Warden / MatrixOS project change reporting

Task: [#83](https://github.com/badwolfzm/pc/issues/83). This document specifies **how to report changes**, not a claim that any feature is complete. See [AGENTS.md](../AGENTS.md), [CONTROL.md](../control/CONTROL.md), [STATUS.md](../control/STATUS.md), and [COORDINATION.md](../control/COORDINATION.md) for active governance.

## Sources and truth order

At each check, record the observed timestamp (UTC), repository and inspected main HEAD SHA. Compare the latest state with the previously **successfully inspected** state:

1. `main` commits/merged PRs: shipped to main, **not necessarily validated at runtime**.
2. Open/closed PRs, exact head SHAs, review threads, CI statuses and run logs: proposals, review decisions and test evidence. A draft is never described as merged.
3. Issues and agent claims: intentions, blockers and work in progress, not implementations.
4. Control/status/coordination docs: decisions and reported progress; reconcile discrepancies against live GitHub state. Board entries may lag.

Use direct URLs to the exact source. For tests record revision, command, environment, exit result and provenance. Distinguish host prototype, native QEMU/TCG guest, KVM on target PC, Linux-personality and actual game execution. A passing host mock does not prove Doom or Albion works. A CI job configured but not run is **pending**, not passing.

## Notification rule

Report only new or changed facts since the last successful checkpoint, with no repeated unchanged headlines. Changes with higher significance are shown first:

- **Critical:** newly discovered authorization bypass, data corruption, guest-to-host escape, leaked private information or test regression that undermines a shipped security boundary. State whether confirmed, suspected, or source-only.
- **Action needed:** owner merge decision, conflicting agent claims, stalled dependency, failed required checks, permissions or hardware access blocking verification.
- **Progress:** merged work, a newly passing exact-commit test, an important draft PR, or a meaningful change to game/compatibility/storage milestones.
- **Routine:** comment-only edits and cosmetic changes may be omitted from push notifications but remain visible through GitHub.

Never auto-approve, auto-merge, close issues, or resolve review threads solely because a notification reports progress. Preserve user/lead authorization and independent security review. Agent claims do not grant ownership of other agents' files.

## Suggested owner-facing report

```text
Warden / MatrixOS update — <UTC time>
Repository: badwolfzm/pc
Observed main: <full SHA>
Since: <last successfully observed UTC time/SHA>

Merged: <change, PR/commit link, what it actually enables>
In review: <PR number, exact head, status/CI, dependencies>
Security/test evidence: <pass/fail/pending, test, revision, environment>
Doom / Albion / Linux compatibility: <measured milestone or explicitly unverified>
Storage/swap/precomputation: <measured milestone or explicitly unverified>
Needs your decision: <specific request or none>
Known conflicts/uncertainty: <stale board, missing CI, overlap, etc.>
Next check: <scheduled policy, not a delivery promise>
```

## Checkpoint and failure behavior

A monitor should persist a comparison checkpoint only after fetching enough of the repository state to make a truthful comparison. If a GitHub request fails or pagination is incomplete, say the view is partial and do **not** advance the affected checkpoint. When history was unavailable, use a clearly marked initial baseline rather than inventing an earlier delta. Do not treat absent results as deleted PRs. Identify force-push/rebase by PR head SHA changes and re-check prior reviews/CI against the new head. Reports are observations, not substitutes for release or security acceptance.

## Scope and handoff

This is a documentation-only, isolated contributor change, not a replacement for the bookkeeping ledger or the lead's GitHub publication workflow. Files changed: only `docs/project-change-reporting.md`. Validation: GitHub readback and PR diff inspection. No kernel, runtime, swap, QEMU or game benchmarks performed by this change. Requires independent coordination review before integration.
