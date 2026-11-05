# Combined change evidence — incomplete template

- Integration candidate SHA: NOT CREATED
- Frozen main SHA: NOT RECORDED
- Source PR SHAs: NOT FROZEN
- Source ownership/lead permission: PENDING
- #24 -> #26 dependency resolved: PENDING
- #31 vs #33 authoritative contract decision: PENDING
- Combined diff independently security reviewed: NOT RUN
- Exact changed-file manifest / ABI impact: NOT PREPARED

| Gate | Command / procedure | Environment | Exit/count | Provenance | Status |
| --- | --- | --- | --- | --- | --- |
| Formatting and diff safety | `git diff --check` | integration worktree | — | — | NOT RUN |
| Rust units | `cargo test` | record OS/toolchain | — | — | NOT RUN |
| Host escape tests | `bash tests/escape/run.sh` | supported Linux/root sandbox | — | — | NOT RUN |
| Compatibility tests | `bash tests/compat/run.sh` | supported Linux | — | — | NOT RUN |
| Kernel QEMU | `bash tests/kernel/run.sh` | TCG or KVM specified | — | — | NOT RUN |
| Network isolation | #24's integration probe | QEMU guest | — | — | NOT RUN |
| Linux ABI negatives | #26 plus #35 when delivered | native-vs-Warden separated | — | — | NOT RUN |
| ELF compatibility classifier | #34's regression tests | Python toolchain | — | — | NOT RUN |
| Target PC WSL2 | `HOST.md` documented tests | actual target PC | — | — | NOT RUN |
| Combined security review | independent reviewer + commit ID | full diff | — | — | NOT RUN |
| Runtime workload | named unchanged binary/game, workload + FPS/latency | specified host/QEMU | — | — | NOT RUN |

## Blockers / deviations

Record each failed or skipped check with the reason, issue owner, proposed fix, and retest SHA. Never turn a host/native result into a Warden guest or PC result.

## Review and authorization

- Reviewer:
- Reviewed combined SHA:
- Findings:
- Remediations and retest:
- Lead approval:
- Explicit user merge approval:
