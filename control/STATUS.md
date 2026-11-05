# Warden / MatrixOS — status board

Maintained by the bookkeeping thread ("Track bookkeeping for all phases").
Source of truth for scope and decisions: `control/CONTROL.md`.
Repo: github.com/badwolfzm/pc. Last updated: 2026-11-05 07:35 UTC.

This board records state only. It does not make decisions and does not write
kernel or module code. "Verified" means a thread ran it; "not verified" means
it did not. Rows are copied from thread reports and the CONTROL.md decision log.

## Role model (user, 2026-11-05 07:38 — supersedes the 07:36 rule)

- **Bookkeeping thread (this one) holds the record of everything.** Workers
  report every delivery and test result to this thread; it writes them into
  STATUS.md and CONTROL.md. This board is the single source of truth.
- **Control thread reads this output and does all pull/push requests** to
  badwolfzm/pc. It is the sole party that touches git.
- Worker threads do not open their own PRs and do not push. They report to
  bookkeeping; the record flows bookkeeping → control → repo.

This supersedes CONTROL.md decision 4 (kernel thread as single sync owner):
the control thread is now the sole pusher, driven by this board.

## Phase roll-up

| Phase | Scope | State |
|---|---|---|
| Phase 0 | Host supervisor baseline (tarball) | Done, on main (823a666) |
| Phase 1 | x86-64 microkernel boots under QEMU, ring-3 under capability model | Done, PR #1 merged |
| Phase 2a | Per-thread address spaces, synchronous IPC endpoints | Done, PR #4 merged |
| Phase 2 review fixes | host-exit auth, alloc-before-validate, heap growth bound | Done, PR #5 merged (main 00677c0) |
| Phase 2b | Static ELF loader, first Linux syscalls via personality server, two isolated apps on one workload | In progress (started by kernel thread) |
| Phase 3 | Rust port of MXSB loader, untyped-memory accounting, heap reclamation; Docker/K8s later | Not started |

Design doc: https://claude.ai/artifact/RgybmAwmXP3ypxfVxGmRju
Kernel plan: https://claude.ai/artifact/Npeq8VSVoMrT32Mo69VLLZ

## Pull requests (badwolfzm/pc)

| PR | Branch | Title | State |
|---|---|---|---|
| #1 | phase1-boot | Phase 1 microkernel boot | Merged |
| #2 | project-files-sync | Project files sync (control, matrixos, uploads) | Merged |
| #3 | project-files-sync | Re-sync: assets, security regressions, GPU ref | Merged |
| #4 | phase2a-aspace-ipc | Phase 2a address spaces + IPC | Merged |
| #5 | review-fixes-bd1e914 | Fix bd1e914 review findings | Merged |

No open PRs. Next expected: a fresh `project-files-sync` PR carrying everything
accepted since #3 (frontend, canvas, superops, codegen, assets shim, tests,
contract revisions), and the kernel's Phase 2b PR.

## Threads

| Thread | Owns | State |
|---|---|---|
| Control (lead) | Scope, rulings, acceptance | Active; cannot message threads until 07:53 UTC |
| Bookkeeping (this) | This board | Active |
| Kernel | Warden kernel, single repo-sync owner | Phase 2b in progress; Phase 2a + review fixes merged |
| Security review | matrixos/reviews/security-review.md | Delivered (wave 0) |
| MXSB contract | matrixos/contracts/mxsb-manifest.md + C reference loader | Contract landed; reference loader NOT yet built |
| GPU command set | matrixos/contracts/gpu-commands.md | Delivered; section 6 revised with PC numbers |
| Bench | matrixos/bench/ | Cloud + PC bandwidth done; superops triangle bench on PC pending |
| Tests | matrixos/tests/ | Harness live; running acceptances |
| Frontend | matrixos/frontend/ | Accepted (F2, F3 fixed) |
| Codegen | matrixos/codegen/ | Delivered (candidate 6); pending acceptance |
| Super-ops | matrixos/superops/ | Accepted (F6 fixed) |
| LFSR lifter | matrixos/lfsr/ | Accepted |
| Canvas | matrixos/canvas/ | Delivered (candidate 4); pending acceptance |
| Assets | matrixos/assets/ | Accepted (F1, F1b); owes PATH trace event in mx_vfs_compat.h |

## Parallel round (launched 2026-11-05 07:35)

New lead: "Control all phases in parallel". Eight worker threads started, all in
`working` state, no deliveries yet. Mapping to phases and carried-over gaps:

| Thread | Targets | Carried-over gap it addresses |
|---|---|---|
| Build two isolated apps running together | Phase 2b | two isolated apps / one workload |
| Run the Linux personality and ELF loader | Phase 2b | ELF loader + first Linux syscalls |
| Add untyped memory and heap budget | Phase 3 | kernel heap reclamation (gap 7) |
| Budget IPC endpoint queues | Phase 2 hardening | IPC queue resource bounds |
| Harden user memory mapping | Phase 2 hardening | per-thread mapping safety |
| Run Rust and QEMU tests on WSL2 | Verification on target host | PC-side test runs (DESKTOP-9NPQNID) |
| Harden the Phase 0 supervisor | Phase 0 gaps | path races, UDP/DNS, per-library compartments |
| Connect game runtime and rendering | Second workload (game server) | canvas/superops/GPU integration |

Prior round's leads are closed: "Control the whole effort" (done) and
"Track bookkeeping and coordination" (blocked). This thread is the active board.

## Wave 1 module acceptance

| Module | Delivered | Accepted | Notes |
|---|---|---|---|
| LFSR lifter | yes | yes | golden 25/26 = baseline, no new regression |
| Assets | yes | yes | F1/F1b fixed; mx_vfs_compat.h shim provided |
| Frontend | yes | yes | mxcc driver F3; mx_input.h shared typed-input header (F2) |
| Super-ops | yes | yes | F6 fixed; gpu_vadd truncation refused via compat shim |
| Canvas | yes | pending | 638 checks, ASan clean; owns consumed_hash defect |
| Codegen | yes | pending | MXSB emitter+validator V1..V25, tiers, lifter hook; 40k checks |

All six wave-1 modules are delivered. Two await acceptance.

## Integrated build (latest reported)

- golden_check.sh: 25/26 (baseline pin held; trace-hash difference recorded as a documented integrated golden from the fuzzer swap).
- security_regressions.sh: 0 failures under gcc (F7 interim via codegen validator; Rust-side F2 skip).
- RCS conformance suite: 92/92 (matrixos/tests/unit/rcs/).
- Accepted modules in integrated build: lfsr (+ compat header), assets (+ VFS shim), frontend, superops (+ compat shim).

## Open gaps (reported to control)

1. **MXSB reference loader not built.** Ruling assigns matrixos/contracts/mxsb-ref/ (checks V1..V26, schema error codes, malformed corpus) to the MXSB thread; it is the F7 owner. Codegen's validator is the interim stand-in and both must agree.
2. **Contract fixes owed by MXSB thread** (from codegen): missing load-address layout rule, unauthenticated inter-section padding, v1 header is 40 bytes not 32. Needs a minor contract bump + reference loader.
3. **Canvas and codegen pending acceptance** by the tests thread (order: canvas then codegen).
4. **Assets owes PATH trace event** in mx_vfs_compat.h read_all/write_all to restore the golden trace hash.
5. **Next sync PR not yet opened** (fresh project-files-sync from main) with all accepted deliveries since #3.
6. **Superops triangle rasteriser PC bench pending**; the 20,000-triangle cap stays until measured on DESKTOP-9NPQNID.
7. **Kernel heap reclamation open** (bump allocator never frees; per-run growth bound is the guard until Phase 3).
8. **avx512 tier build-only / not verified** on the target PC (AVX2 only, no AVX-512).
9. **Frontend** to take tier flags from codegen's MX_TIER_MARCH[] in mx_tiers.h rather than retyping them.

## Handover and standing integrity checks

First-round bookkeeping thread ("Track bookkeeping and coordination") closed its
board and handed over to this one at 07:40. Its frozen first-round board, with a
dependency diagram, conflict log and a 06:48–07:38 change log, is archived at
https://claude.ai/artifact/3qeRy2v8qv9iAMNuQyhGr3 (history only).

Standing checks (re-verified by this thread at 07:40):

- **Uploaded C baselines untouched.** uploads/hearth/ holds the two files at
  36722 and 3570 bytes, timestamp 2026-11-04 17:28. No thread edits them.
- **Folder ownership intact.** All ten matrixos/ folders match the CONTROL.md
  owner layout (assets, bench, canvas, codegen, contracts, frontend, lfsr,
  reviews, superops, tests); no thread has written outside its own folder.

## Recent audit findings (user, 2026-11-05)

- **mxcc bypass (frontend):** `--` skipped the deny policy. Fixed in frontend 1.0.1 with a live-plugin regression test. Harness weakness (missing compiler counted as pass) also fixed. Tests thread to re-run frontend acceptance on 1.0.1 and add the `--` plugin case to security_regressions.sh F3.
- **Canvas integer overflow:** ghost_canvas.c overflows with INT_MAX/INT_MIN coordinates. Canvas owns this finding: fix with checked arithmetic + regression test. Tests thread to add the overflow case as a new check.

## Discrepancy to resolve

- **Frontend thread believes no repo exists.** Its 07:37 report says "no GitHub
  repository exists yet" — it still thinks code stays in project files. But
  badwolfzm/pc exists with five merged PRs. Consequence: frontend 1.0.1 and the
  canvas fixes are not yet synced as PRs. Control thread has been told to sync
  them. Under the new role model, control is the pusher, so workers holding this
  stale belief is harmless as long as they report deliveries to bookkeeping.
