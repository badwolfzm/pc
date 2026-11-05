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
| Phase 0 | Host supervisor baseline (tarball) | Done; hardening merged (PR #13: path races, exec verify, per-process library grants) |
| Phase 1 | x86-64 microkernel boots under QEMU, ring-3 under capability model | Done, PR #1 merged |
| Phase 2a | Per-thread address spaces, synchronous IPC endpoints | Done, PR #4 merged |
| Phase 2 review fixes | host-exit auth, alloc-before-validate, heap growth bound | Done, PR #5 merged (main 00677c0) |
| Phase 2b | Static ELF loader, first Linux syscalls via personality server, two isolated apps on one workload | **Merged (PR #16)** — two isolated apps over a user-space Linux personality server; static HTTP server serves a client in a second isolated process. Cloud-VM verified, not on PC |
| Phase 3 | Rust port of MXSB loader, untyped-memory accounting, heap reclamation; Docker/K8s later | Started: untyped memory + heap reclamation merged (PR #14). MXSB Rust port and Docker/K8s still ahead |
| Wave 2 (MatrixOS integration) | First integrated build across all six modules | **Complete** (08:06): all six accepted, security_regressions 32 pass / 0 fail, golden 25/26 |

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
| #6 | review3-mm-wx-ipc | kernel: check-before-alloc, W^X mappings, fixed-capacity IPC queues | Merged |
| #7 | harden-user-mapping | Harden user memory mapping | Closed, NOT merged — superseded by #6 |
| #8 | project-files-sync-2 | Sync frontend 1.0.1, canvas, superops, codegen, assets shim, tests | Merged |
| #9 | kernel-revoke-fix | caps: revoke() allocates nothing; counting-allocator + in-guest flood | Merged |
| #10 | gpusim | gpusim: simulated memory-based GPU in CPU RAM (prototype) | Merged |
| #11 | adversarial-tests-v2 | Kernel tests: wrong-cap, mid-flight revocation, exhaustion floods | Merged |
| #12 | project-files-sync-3 | Sync canvas fixes, test oracles, codegen notes, control plan, board | Merged |
| #13 | phase0-hardening | Phase 0 supervisor: close path races, verify exec, per-process library grants | Merged |
| #14 | phase3-untyped | Phase 3: untyped memory pays for every kernel object; heap frees and reclaims | Merged |
| #15 | project-files-sync-4 | Sync MatrixOS wave 2 (codegen, frontend 1.0.2), control plan, board | Merged |
| #16 | phase2b-isolated-apps | Phase 2b: two isolated applications over a user-space Linux personality server | Merged |

Main is at **cc904bc**. No open PRs. Staged and not yet pushed: the Pong guest
(needs the 60..64 syscall ruling) and the tests thread's dev-profile fix (below).

## Threads

| Thread | Owns | State |
|---|---|---|
| MatrixOS lead ("Control the whole effort") | MatrixOS scope, rulings, module acceptance | Active; sends MatrixOS state changes here |
| Kernel lead ("Control all phases in parallel") | Kernel/phase scope; sole git pusher | Active |
| Bookkeeping (this) | The record / this board | Active |
| Kernel | Warden kernel | Phase 2b in progress; through PR #6 merged |
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

## MatrixOS sync-ready manifest (convention, 07:57)

Convention with the MatrixOS lead: a folder is mirror-ready only when the lead
says **"accepted, ready to sync"** with its path. The kernel lead (sole pusher)
mirrors exactly those rows; nothing else.

**Every matrixos/ folder is now accepted and ready to sync** (MatrixOS lead, 08:06):
- matrixos/lfsr/
- matrixos/assets/ (including mx_vfs_compat.h)
- matrixos/superops/
- matrixos/canvas/
- matrixos/contracts/ (gpu-commands.md v1, mxsb-manifest.md 2.0.1 + schema, gpu-commands-ref/)
- matrixos/bench/ (cloud and pc)
- matrixos/reviews/
- matrixos/tests/
- matrixos/codegen/ — synced via PR #15 (updated: 43 mutations give identical verdicts to mxsb-ref)
- matrixos/frontend/ — 1.0.2 synced via PR #15
- matrixos/contracts/mxsb-ref/ — NEW, accepted, ready to sync (F7 primary oracle)
- matrixos/tests/ — updated (F7 wired to mxsb-ref primary + codegen second oracle; integrated 34 PASS/0 FAIL), ready to sync

Codegen and frontend 1.0.2 are on main (PR #15). Outstanding sync for the kernel
lead: matrixos/contracts/mxsb-ref/ and the matrixos/tests/ F7 wiring update.

## Prototype handoff queue (rapid prototyping — user, 07:49)

Mode: each thread hands off a *working prototype* as soon as it runs; the
control thread merges from this queue without waiting for the full module.
Workers report here (via bookkeeping); control pulls, PRs and pushes. "Staged"
= prototype ready at a path, not yet pushed. "Merged" = on main.

| Thread / work | Prototype state | Where | Control action |
|---|---|---|---|
| Kernel: map_user W^X, check-before-alloc, fixed IPC queues | Merged | PR #6 | done |
| Kernel: revoke() allocates nothing | Merged (cross-verified by tests thread) | PR #9 | done |
| Harden user memory mapping | Superseded by #6 | PR #7 closed unmerged | none |
| Tests: adversarial suite (wrong-cap, mid-flight revoke, floods) | Merged | PR #11 | done |
| Add untyped memory and heap budget | Merged — untyped memory pays for every object; heap frees/reclaims | PR #14 | done |
| Harden the Phase 0 supervisor | Merged — path races, exec verify, per-process library grants | PR #13 | done |
| gpusim (GPU prototype) | Merged — simulated memory-based GPU in CPU RAM | PR #10 | done |
| Game runtime host lib (Run Pong inside Warden) | Delivered, cloud-only: test_runtime 229 pass, ASan/UBSan clean; 720p 0.6–2.8 ms/frame, paced demo 30.8 fps | matrixos/runtime/ (NEW folder) | sync after lead accepts |
| Pong as a native Warden guest | Delivered, cloud TCG only (not independently verified): pong.sh 15/15 ALL HELD, 30 fps 0 missed slots, 2.7 ms median input latency, heap +0, hostile writer/forger/spinner refused/killed; escape suite 54/54 | handoff/pong-in-warden/ (series/0001 on main 5a817c2, HANDOFF.md, pong-screen.png, pong-serial.log) | needs syscall 60..64 ABI ruling + rebase on aa06bf0 before push |
| MatrixOS sync (canvas fixes, oracles, codegen notes, board) | Merged | PR #12 | done |
| Run the Linux personality and ELF loader (Phase 2b) | Merged (PR #16) | on main | done |
| Build two isolated apps running together | Merged (PR #16) — HTTP server + client, two isolated processes, loopback | on main | done |
| Budget IPC endpoint queues | Fixed-capacity queues landed in #6 — confirm any remaining scope | — | — |
| MatrixOS codegen + frontend 1.0.2 sync | Merged (PR #15) | on main | done |
| Tests: dev-profile kernel suite fix | Staged, cloud-VM verified: release 131/131, dev 131/131 | kernel/dev-profile-fix/0001-*.patch (git am on cc904bc; NOTE.md + before/after logs) | push (clean on current main) |

**Tests thread results (cloud VM, QEMU 8.2.2 TCG, no KVM, not verified on WSL2/PC):**
cargo test 23/23, kernel 45/45, escape 44/44, compat 13/13. harden-user-mapping.patch
applies clean to main and holds 63/63. Adversarial suite (wrong-capability use,
mid-flight revocation, exhaustion floods, stack/data overrun) 92/92 after one fix.
Independently found the SYS_REVOKE per-call allocation (5,000 calls = +475 KiB,
ring 3 could drain the arena) and fixed it allocation-free in warden-caps — the
same defect the kernel thread's 0001 patch fixes. Open question for kernel thread:
badge re-write through GRANT (one ignored test).

**Base-commit skew (flag for control):** the tests thread's patch series
(harden-user-mapping.patch, then 0001/0002) is based on main 00677c0, *before*
PR #6 (0db603c) landed the map_user W^X and check-before-alloc work. #6 already
contains the mapping hardening, so control must rebase/dedupe this series onto
current main (6d9f979) before landing 0001/0002; do not double-apply.

Phase 2b design notes (for the two-apps thread) are at
/mnt/project-files/handoff/phase2b-from-kernel/README.md; the kernel thread
wrote no Phase 2b code.

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
| Frontend | 1.0.2 | yes | mxcc driver F3; mx_input.h (F2); `--` bypass fixed; 1.0.2 sources tier flags from codegen's mx_tiers.h with a passing drift test |
| Super-ops | yes | yes | F6 fixed; gpu_vadd truncation refused via compat shim |
| Canvas | yes | yes | 638 checks, ASan clean; consumed_hash + INT overflow fixed |
| Codegen | yes | yes | MXSB emitter+validator; tiers; lifter hook; mxsb_test 40113, tiers 25, lifter 21; harness MXSB contract 43/43 (interim F7 stand-in) |

**All six wave-1 modules are accepted into the first integrated build** (08:05,
integration owner). Frontend 1.0.2 (tier-flag sourcing) is a version bump still
pending re-acceptance; 1.0.1 is the accepted baseline.

## Integrated build (integration owner, 08:05)

First integrated build assembled; all six modules in. Acceptance rule met per
module (no new failure vs the frozen baseline; every owned finding fixed).

- security_regressions.sh: baseline 15 failures → **integrated 0** (now 34 PASS, 0 FAIL). F7 is now wired to a real oracle: matrixos/contracts/mxsb-ref/ primary + codegen as a second oracle (no longer interim).
- golden_check.sh: 25/26 on baseline and integrated (documented GOLDEN_TRACE=0x3b3805a77980eee1; the one failure is the frozen baseline wrapper bug).
- Unit suites: rcs 92/92, canvas 40/40 (incl. INT_MAX/INT_MIN block), mxsb contract 43/43 (interim F7 stand-in).
- gpu-commands-ref independent conformance: 92/92.
- Per-module own tests: lfsr 394,115; assets 47 (+ harness F1/F1b 10); frontend 1.0.1 typed-input 48 + cells 57 (Rust skipped, no cargo) + F2 3/F3 7; superops 203; canvas 759+22 (golden ghost hash f7da60a90fb9597f); codegen mxsb_test 40113 + tiers 25 + lifter 21.
- fuzz/mx_fuzz_compat.h fixes finding 5.
- Contracts: mxsb-manifest.md 2.0.1 + schema; gpu-commands.md v1 with PC-measured budget.
- Everything up to PR #8 is on main; anything accepted after #8 is a handoff for the kernel lead to sync.
- F7 remains interim (codegen's mxsb_contract.c) until contracts/mxsb-ref/ is built by the MXSB thread.

## Open gaps (reported to control)

1. ~~MXSB reference loader not built.~~ **Resolved.** matrixos/contracts/mxsb-ref/ is built and accepted: run_tests.sh 348/0, 0 divergences from codegen (68-file corpus, goldens, 20k mutation fuzz under ASan/UBSan), contract test 43/43. It is now the F7 primary oracle, codegen the second oracle. Ready to sync.
2. **Contract fixes owed by MXSB thread** (from codegen): missing load-address layout rule, unauthenticated inter-section padding, v1 header is 40 bytes not 32. Needs a minor contract bump + reference loader.
3. **Canvas and codegen pending acceptance** by the tests thread (order: canvas then codegen).
4. **Assets owes PATH trace event** in mx_vfs_compat.h read_all/write_all to restore the golden trace hash.
5. **Next sync PR not yet opened** (fresh project-files-sync from main) with all accepted deliveries since #3.
6. **Superops triangle rasteriser PC bench pending** (NOT an acceptance gate). The span-threshold fix (scalar below 96 px, SIMD above) is in matrixos/superops/; the perfcheck was reworked to compare only where the paths differ and now passes 3/3 in-container (~20 s each), unit suite 203/203 under ASan/UBSan. Still open: PC runs of `make tri-sweep` and `make perfcheck` (bench thread) to set the 20,000-triangle cap on DESKTOP-9NPQNID; DRAW_TRIANGLES texture sampling rule (next wave); NEON build untested.
7. ~~Kernel heap reclamation open.~~ **Resolved** in PR #14 (Phase 3): untyped memory pays for every kernel object and the heap frees and reclaims.
10. **Pong guest needs an ABI ruling:** syscall numbers 60..64 requested from the kernel lead (recorded in CONTROL.md). Blocks the Pong-in-Warden push until assigned. New files: matrixos/runtime/ (game runtime host lib) and control/contracts/runtime.md.
11. **Dev-profile test false-fail (fixed, staged):** on main cc904bc, PROFILE=dev run.sh killed "capability attacks" and "exhaustion floods" as budget deaths (128 checks, 3 failed). Fix makes the flood loops yield every 512 iterations, no check changed; release 131/131 and dev 131/131 after. Patch at kernel/dev-profile-fix/, not yet pushed.
12. **User-action blocker — PC/WSL2 verification.** Everything so far is verified on the cloud VM under QEMU TCG only. Nothing is verified on the user's PC (DESKTOP-9NPQNID) or WSL2, which is the stated acceptance target (CONTROL.md decision 5). The tests thread is waiting on the user to reply in its own thread so it can reach the device.
8. **avx512 tier build-only / not verified** on the target PC (AVX2 only, no AVX-512).
9. ~~Frontend to take tier flags from codegen's MX_TIER_MARCH[] in mx_tiers.h.~~ **Resolved** in frontend 1.0.2 (sourced from mx_tiers.h with a drift test; pending tests-thread re-acceptance, then a kernel-lead sync handoff).

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

## Discrepancy (resolved)

- **Frontend "no repo exists" belief — resolved.** Frontend's 07:37 report said
  no repo existed. PR #8 (project-files-sync-2, merged 07:43) synced frontend
  1.0.1, canvas, superops, codegen, assets shim, tests and contracts to main.
  Under the role model this is harmless anyway: workers report to bookkeeping,
  control pushes.
