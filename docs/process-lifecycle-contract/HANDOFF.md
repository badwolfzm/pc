# Claim and handoff: issue #28, process lifecycle security contract

- Task ID: process-lifecycle-contract-28-20261105
- Owner: ChatGPT GitHub contribution session, user-authorized to work on repository
- State: draft / awaiting independent review
- Base commit: `70bc55e77a588561ded1ffaaace067283608e626`
- Branch: `agent/process-lifecycle-contract-28-20261105`
- Owned paths: `docs/process-lifecycle-contract/**` only
- Scope: a fail-closed design and acceptance-test specification for process creation, executable replacement, signals and futexes; **no kernel changes**
- Dependencies: issue #28; open PRs #24 and #26; ELF follow-ups #29/#37; entropy issue #36
- Overlap review: current open PRs #24, #26, #33, #34, #37 and #39 inspected by metadata; #34 and #37 changed-file manifests checked. This task avoids all kernel and active tool paths. Other agents' unpublished local changes are not visible.
- Validation: GitHub source/coordination documents and kernel source read on 2026-11-05; repository file readback and PR changed-file listing are required post-publication.
- Not verified: no `cargo test`, compilation, QEMU, WSL2, target-PC, native-game or penetration testing; documentation has no runtime claims.
- Independent review: pending, mandatory before code implementation.
- Next owner: kernel/ABI lead to review syscall ownership, error behavior, quota and rollback requirements and split implementation into independently testable tasks.
- Merge policy: draft only; no automatic merge; record explicit lead approval and security review before landing.
