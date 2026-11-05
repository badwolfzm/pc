# Claim and handoff: issue #28, process lifecycle security contract

- Task ID: process-lifecycle-contract-28-20261105
- Owner: ChatGPT GitHub contribution session, user-authorized to work on repository
- State: draft / awaiting independent review
- Base commit: `70bc55e77a588561ded1ffaaace067283608e626`
- Branch: `agent/process-lifecycle-contract-28-20261105`
- Owned paths: `docs/process-lifecycle-contract/**` and `.github/workflows/lifecycle-abi-audit.yml` only
- Scope: a fail-closed design and acceptance-test specification for process creation, executable replacement, signals and futexes; **no kernel changes**
- Dependencies: issue #28; open PRs #24 and #26; ELF follow-ups #29/#37; entropy issue #36
- Overlap review: current open PRs #24, #26, #33, #34, #37 and #39 inspected by metadata; #34 and #37 changed-file manifests checked. This task avoids all kernel and active tool paths. Other agents' unpublished local changes are not visible.
- Validation: Published audit tool and tests were fetched from the branch. GitHub Actions run 42012207960 on commit d0db3d22d25dff70b1c27098e711581aa4efca60 completed successfully (Ubuntu runner); job 127193761213 logs explicitly show 6 unit tests passed, source inventory completed and artifact uploaded. This is source-level only. Other-agent source ownership was rechecked against open PRs. Main has advanced since the original base; integration needs fresh comparison/review.
- Not verified: no Rust compilation/tests, QEMU, WSL2, target-PC, native-game or penetration testing; documentation and Python inventory do not make runtime security claims. Independent review not obtained.
- Independent review: pending, mandatory before code implementation.
- Next owner: kernel/ABI lead to review syscall ownership, error behavior, quota and rollback requirements and split implementation into independently testable tasks.
- Merge policy: draft only; no automatic merge; record explicit lead approval and security review before landing.
