# Agent claims, evidence and handoffs

This is an onboarding and handoff convention, not a replacement for
[CONTROL.md](CONTROL.md) or [STATUS.md](STATUS.md). Existing ownership and the
bookkeeping/lead publication roles remain in force. Current user instructions
take precedence over this convention.

The broader agent guide and branch-local claims/handoffs are being introduced
in [PR #26](https://github.com/badwolfzm/pc/pull/26), under `docs/agents/`.
This document supplies templates and live-state checks to complement it;
it does not maintain a competing active-task ledger or take ownership of
that agent's files. Check the PR's current state before using those paths.

## Start from live state

1. Read the control plan, status board, module documentation and contracts.
2. Record the current main SHA and inspect open PRs and task claims. Read the
   changed-file lists of potentially overlapping PRs, not just their titles.
3. Reconcile stale board entries against commits/PRs. Report discrepancies to
   bookkeeping with references; leave its board to its owner.
4. Select a bounded task and confirm its paths are available under the
   ownership plan. Record the claim before editing.

Do not treat an old "no open PRs" statement or an old handoff base as current.
Branches and local patches belonging to other sessions may not be visible.
If another owner is unreachable, work in an available area or prepare a
request; silence is not transfer of ownership.

## Claim format

Submit this record through bookkeeping; the lead may publish it as a GitHub
issue when permitted. An explicitly user-authorized contributor may publish
its own claim. Use a stable task ID in the branch, handoff and PR.

```text
Task ID / title:
Owner (agent + session or thread):
State: claimed | working | blocked | review-ready | merged | released
Base commit (full SHA):
Branch:
Owned paths:
Scope / acceptance criteria:
Dependencies (PR + exact commit, or none):
Overlap checked (PRs + existing ownership):
Validation commands planned:
Blockers / requests:
Last updated (UTC):
```

A claim records intent; GitHub issues are not an atomic file lock. Before
editing and again before delivery, check for newly overlapping claims/PRs.
If there is a collision, stop overlapping edits and let the lead resolve it.
Do not take over a claim just because its timestamp is old. Release abandoned
claims explicitly; mark blocked claims with what would unblock them.

## Evidence format

For each check, record:

| Field | Required information |
|---|---|
| Revision | Exact commit, plus any uncommitted changes tested |
| Command | Command and relevant options/environment overrides |
| Environment | OS, toolchain; host vs Warden; TCG/KVM; cloud vs target PC |
| Result | Exit status, counts if applicable, and log/artifact reference |
| Limits | Skips, unavailable dependencies and work not exercised |
| Provenance | Run by this session, independently rerun, or reported by another agent |

For performance claims also record hardware, build flags, workload, reference
output checks and sampling method. Never relabel a cloud result as a PC result.
For documentation-only work, check links and the diff; runtime tests can be
marked not applicable, with the reason.

## Handoff format

Use the existing handoff location and patch-series convention from CONTROL.md.
Include this summary in HANDOFF.md or the PR body:

```text
Task / claim reference:
Owner:
Base commit and delivered commit:
Changed paths:
Problem and resulting behavior:
Dependencies and landing order:
Validation evidence:
Not verified:
Independent review (reviewer, revision, findings and disposition):
Remaining work / requests to other owners:
Suggested PR title and body:
```

Keep a handoff's referenced patch series consistent with its base SHA. If main
moved, inspect the intervening changes; rebase or regenerate when needed and
rerun affected checks. Do not resolve security-logic conflicts by guessing.
The lead performs the integration checks required by CONTROL.md before landing.

## Review and publication

- Keep one concern per branch/PR; reference the claim and dependencies.
- Mark delivery review-ready only after its applicable checks have results.
  If required checks are unavailable or unresolved, deliver a draft and name
  the outstanding gate.
- A reviewer checks the actual diff at a named revision against contracts and
  acceptance criteria. Attribute the review honestly; reviewing is distinct
  from executing tests.
- Request bookkeeping to record the delivery and the lead to publish it under
  the standing workflow. Only explicitly user-authorized exceptions bypass
  that publication route.
- A published PR is not a merge. Follow the current merge authorization and
  report the actual merged SHA before calling the work merged.

Separate chats/services learn about this work only when they read the shared
records. Creating these documents does not automatically notify or start them.
