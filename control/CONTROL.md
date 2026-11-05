# Warden / MatrixOS — control plan

Lead thread: "Control the whole effort". Status board: "Track bookkeeping and coordination".
Updated: 2026-11-05. Source of truth for scope: the design doc
(https://claude.ai/artifact/RgybmAwmXP3ypxfVxGmRju) and the kernel plan
(https://claude.ai/artifact/Npeq8VSVoMrT32Mo69VLLZ).

## Decisions already made (do not reopen without the lead)

1. No thread edits the uploaded MatrixOS C file in place. It is the baseline.
   Each thread writes its own module under its own folder (layout below).
2. Contracts before code. The MXSB manifest spec and the GPU command-set spec
   are written first; every other compiler/GPU thread builds against them.
3. Reproducible builds: fixed SIMD tiers (baseline, avx2, avx512) instead of
   `-march=native`. The `runtime-fast` profile loses `-march=native`.
4. Repository: github.com/badwolfzm/pc (user-created). Phase 0 kernel is on
   main (commit 823a666). Kernel work continues there on branches with PRs.
   User rule (2026-11-05): everything is saved to GitHub, text files and
   makefiles included. Project files stay the working area; the kernel thread
   is the single sync owner and mirrors matrixos/ and control/ into the repo
   on a "project-files-sync" branch via PR, re-synced after each accepted
   delivery. Module threads do not open their own PRs.
5. CPU target: x86-64 first (user said "64 bit"; ARM64 in Phase 2).
7. Permission names: abi/warden-abi.json is the source of truth. The design
   doc now says proc.spawn (not process.spawn). A `render` permission does not
   exist yet; the GPU command set thread requests it, the kernel thread adds
   it as an additive v0.2 change.
6. Honesty rule: no thread claims a speedup, a security property, or a test
   result it did not measure or run. Say "not verified" when it is not.

## Folder layout (project files)

    control/                      this plan, decision log
    kernel/                       Warden kernel (Phase 0 tarball lives here)
    matrixos/contracts/           mxsb-manifest.md, gpu-commands.md  (wave 0)
    matrixos/reviews/             security-review.md                 (wave 0)
    matrixos/bench/               memory-bandwidth results           (wave 0)
    matrixos/frontend/            compiler driver, profiles, typed input
    matrixos/codegen/             code generation, SIMD tiers
    matrixos/superops/            SIMD super-op kernels
    matrixos/lfsr/                LFSR lifter pass
    matrixos/canvas/              dirty-bit compositing
    matrixos/assets/              asset streaming from storage
    matrixos/tests/               test and benchmark harness (integration owner)

Each thread owns only its folder. Something another folder needs goes through
its owner, not by editing their files.

## Work order

Wave 0 (now, in parallel, no dependencies):
- Review MatrixOS C source for security -> matrixos/reviews/security-review.md.
  Findings go to every wave-1 thread before they design their module.
- Upgrade MXSB manifest format -> matrixos/contracts/mxsb-manifest.md
  (header struct, caps_required bits, manifest_hash rule, version policy).
  Must map caps onto the kernel's abi/warden-abi.json permission names.
- Design GPU command set from storage -> matrixos/contracts/gpu-commands.md
  (command list, argument validation, bandwidth budget per command).
- Benchmark memory bandwidth for GPU -> matrixos/bench/. Its numbers gate
  the 30 fps budget in the design doc; report measured, not assumed.
- Build tests for compiler and GPU -> plan skeleton now; concrete cases once
  the two contracts land.

Wave 1 (start when both contracts exist; read the security review first):
- Frontend, codegen, SIMD super-ops, LFSR lifter, dirty-bit canvas, asset
  streaming. Each delivers: module source, its own unit tests, a short
  README with what is verified and what is not.

Wave 2 (integration):
- Tests thread assembles one build and one harness run across all modules.
- Bookkeeping thread marks the board; lead signs off.

Kernel track (parallel): Phase 1 boot scaffold starts now on a branch in
badwolfzm/pc, x86-64, PR against main.

## Known conflicts and how they are settled

- Frontend thread planned a lexer/parser/type checker. The baseline has none:
  the "compiler" is a clang wrapper plus a loop-signature detector. Frontend
  scope is: the driver CLI, build profiles, SIMD tiers, and the typed-input
  rule (input values 0..0x10FFFF, control values above). No new language.
- SIMD super-ops and GPU command set overlap on the op list. The command set
  spec owns the list and the validation rules; super-ops owns the kernels.
- LFSR lifter and codegen both touch loop detection. Lifter owns detection
  and the affine composition; codegen only calls it.
- Canvas and asset streaming both read from storage. Assets owns the storage
  reader and the cache; canvas consumes frames/tiles from it.

## Decisions the user owns (open)

1. (answered) GitHub repository: badwolfzm/pc.
2. (answered) First CPU target: x86-64.
3. (answered, delegated to lead) Priority when goals conflict: escape
   resistance. A thread drops a syscall or a speedup before it widens the
   boundary.
4. (answered, delegated to lead) First real workload: a small static web
   server (nginx-class) behind the network container. It is the simplest real
   internet-facing input and drives net.listen, fs.read and epoll first. A
   lightweight game server is the second workload.
5. (answered) Host: the user's own Windows PC (DESKTOP-9NPQNID). Ruling:
   Phase 0 and the build run under WSL2; the Phase 1 kernel boots in QEMU
   under WSL2 with nested KVM where available, else QEMU with WHPX on Windows.
   Benchmarks must be measured on that PC, not in cloud containers.
6. (answered, delegated to lead) Standalone warden-run CLI. No Docker or
   Kubernetes integration until Phase 3.

## Decision log

- 2026-11-05 lead: decisions 1-6 above set; folder layout and wave order set.
- 2026-11-05 lead: repo badwolfzm/pc and x86-64 confirmed by user; design doc renamed process.spawn -> proc.spawn; render permission to be added to ABI v0.2; kernel Phase 1 scaffold approved to start.
- 2026-11-05 lead: user wants it to run on their PC and wants findings implemented. Host ruling above. Wave 1 released: security review and MXSB contract are in; frontend, codegen, lfsr, assets code now; superops and canvas start when gpu-commands.md lands. Kernel Phase 1 also closes the three Phase 0 gaps (path races, UDP/DNS, per-library compartments) where Phase 1 does not make them moot.
- 2026-11-05 lead: user said "do what you think is best" on the remaining decisions; defaults 3, 4 and 6 recorded above. All six user decisions are now closed.
- 2026-11-05 lead: tests skeleton delivered (wave 0 item 5). Acceptance rule adopted: per module, its code introduces no new security_regressions.sh failure and fixes every finding it owns, and golden_check.sh still pins the baseline; the integrated wave 2 build must pass security_regressions.sh with zero failures. Run scripts with bash; the project-files mount is noexec. Extra baseline defects are listed in the tests plan doc and assigned to the module that owns the code.
- 2026-11-05 lead: LFSR lifter delivered (first wave 1 module): matrixos/lfsr/, verified against the naive loop and fuzzed, ASan/UBSan clean. Pending: tests thread runs security_regressions.sh against it; codegen lowers to mxl_insn.
- 2026-11-05 lead: bandwidth benchmark delivered (wave 0 item 4, cloud machine only, labeled). Single-core 12 GB/s read; composite+convert+encode 3 to 7.4 ms per 720p frame vs 6.5 to 10.5 ms in the design doc. Design doc numbers hold or are conservative; no doc change until the user's PC numbers arrive. Rerun on DESKTOP-9NPQNID pending user connecting the device.
- 2026-11-05 lead: LFSR lifter accepted (golden 25/26 same as baseline, no new regression, no finding owned). Lifter to add a fair bench row including plan build or a naive row beside it. VFS 512-byte path truncation assigned to the assets thread (owner of the storage reader, finding 1).
- 2026-11-05 lead: user asked to save everything to GitHub. Kernel thread syncs project files into badwolfzm/pc as PR; layout mirrors project files (matrixos/contracts stays under matrixos/). Kernel PR #1 (phase1-boot) is open: boots under QEMU, 27/27 in-guest checks, ABI v0.2, HOST.md for WSL2.
- 2026-11-05 lead: asset streaming delivered (matrixos/assets/), covers findings F1 and F1b with openat2 RESOLVE_BENEATH|NO_SYMLINKS, 47 tests pass under ASan/UBSan. Sent to tests thread for acceptance. SHA-256 verifier at ~255 MB/s per core is the SSD bottleneck; SHA-NI/BLAKE3 later.
- 2026-11-05 lead: GPU command set contract (RCS v1) landed at matrixos/contracts/gpu-commands.md with reference impl. Wave 0 complete except PC bench rerun. Super-ops and canvas released. PR #2 re-synced at b85c9be.
- 2026-11-05 lead: user said merge; PR #1 (kernel Phase 1) and PR #2 (project-files sync) merged into main of badwolfzm/pc. Future syncs: fresh project-files-sync branch from main per accepted delivery. Standing merge policy still on the user's card.
- 2026-11-05 lead: assets accepted (F1, F1b fixed in module, 9/9 checks, 47/47 own tests). Gap: baseline mx_vfs remains in the integrated build; assets thread to provide an mx_vfs-shaped shim over asset_store so the integrated build passes F1/F1b. Hard-link caveat documented via SHA-256 content naming. Re-sync requested.
- 2026-11-05 lead: frontend delivered (matrixos/frontend/): mxcc driver fixes F3, mx_input.h is the shared typed-input header (F2), shared cell vectors pass in C and against the kernel's cells.rs. Sent to tests thread for acceptance. VFS truncation already with assets.
- 2026-11-05 lead: PC benchmark done on DESKTOP-9NPQNID (i3-14100, measured inside the desktop app's Hyper-V Linux VM, 1 core 2 threads, AVX2 only, no AVX-512). One core: read 20.8 GB/s, copy 19.7; composite+convert+encode 2.9 to 5.6 ms per 720p frame vs doc 6.5 to 10.5 ms. Bandwidth is not the 30 fps limiter. L3 is slow (106 ns): render tiles must fit L2 (~1 MB). Results: matrixos/bench/pc/. Consequence: the avx512 tier cannot be verified on the target machine; baseline and avx2 are the tiers that matter, avx512 stays build-only.

## Notices for threads (lead cannot message threads until 07:53 UTC; read this)
- superops, canvas: use matrixos/frontend/include/mx_input.h (mx_validate_len) for input and length checks; do not write your own cell type.
- codegen, superops: target PC has AVX2 and no AVX-512. Verify baseline and avx2 tiers; avx512 is build-only and marked not verified.
- canvas: tiles must fit L2 (about 1 MB) on the target; L3 latency is 106 ns.
- GPU command set: revise section 6 with matrixos/bench/pc/ numbers.
- kernel: next sync should include matrixos/frontend/ once the tests thread accepts it, and matrixos/bench/pc/.
- 2026-11-05 lead: canvas delivered (matrixos/canvas/, 638 checks, ASan clean, consumed_hash fix as a patch file). Candidate 4 for acceptance. PR #3 (sync, 25cb572) open; waits on the user's merge policy card.
- notice for tests thread: run acceptance on matrixos/canvas/ after frontend; canvas owns the consumed_hash defect and no review finding.
- notice for canvas thread: the security review is at matrixos/reviews/security-review.md (landed 06:56); re-read it now.
- 2026-11-05 lead: assets shim delivered (matrixos/assets/mx_vfs_compat.h, drop-in for the baseline mx_vfs block). Notice for tests thread: swap the shim into the integrated build the way the lifter compat header was swapped, and re-run F1/F1b/F4 against it; this should take the integrated failure count from 14 down by at least 3.
- 2026-11-05 lead: PR #3 merged on the user's word. Main holds assets, frontend docs/tests, bench/pc, CONTROL.md.
- 2026-11-05 lead: gpu-commands.md section 6 revised with PC numbers; caps kept conservative until triangle cost is measured. Damage tiles added for canvas. Include in next sync.
- 2026-11-05 lead: super-ops delivered (matrixos/superops/, 202 checks, F6 fixed, gpu_vadd truncation refused via compat shim). Candidate 5 for acceptance. Risk flagged: 20k small triangles cost about 10 ms on the cloud box, close to a third of the 33 ms frame; the command set's 20,000 triangle cap stays until measured on the PC.
- notice for tests thread: acceptance order now frontend, canvas, superops; swap mx_superops_compat.h into the integrated build for F6.
- notice for bench thread: measure the superops triangle rasteriser on the PC (bench_superops.c) so the command set can set the triangle cap.
- 2026-11-05 07:18 lead: USER SAID "start Phase 2". Kernel thread: begin Phase 2 of the Warden kernel plan now, on a branch in badwolfzm/pc: per-thread address spaces, IPC endpoints, and the Linux personality server aimed at the first workload (nginx-class static server: net.listen, fs.read, epoll first). Same permission names and conventions as Phase 1 and ABI v0.2. Report results and permission prompts to the user in your thread.
- 2026-11-05 lead: frontend accepted (F2, F3 fixed). Integrated build with lfsr + VFS shim: golden 25/26 unchanged, F1/F1b pass; security_regressions down from 14 to 3 (F4, F5, F6 pending retarget/adapt). RCS conformance suite 92/92 at matrixos/tests/unit/rcs/. Accepted so far: lfsr, assets, frontend. Pending acceptance: canvas, superops. Outstanding: codegen.
- notice for kernel thread: next sync (fresh PR) should carry matrixos/frontend/ (accepted), matrixos/canvas/, matrixos/superops/, matrixos/assets/mx_vfs_compat.h, matrixos/tests/ updates, revised gpu-commands.md.
- 2026-11-05 lead: Phase 2 started by the kernel thread (coordinator routed the go). PR phase2a-aspace-ipc open: per-thread address spaces, synchronous IPC endpoints with badges, 41 in-guest checks. Next slice: static ELF loader + first Linux syscalls via personality server.
- 2026-11-05 lead: superops accepted (F6 fixed). Integrated build: security_regressions.sh 0 failures under gcc (F7 and Rust-side F2 skip). Replacement fuzzer closes F5. Open: golden trace hash differs because the assets VFS shim omits the PATH trace event; assets thread to emit it (prefer restoring the event over a new golden).
- RULING: the MXSB thread owns the C reference loader (matrixos/contracts/mxsb-ref/mxsb_load.h/.c) implementing checks V1..V26 with the error codes from the schema, plus a test corpus of malformed binaries. Codegen's emitter must pass it. The kernel ports it to Rust in Phase 3's policy service. Tests thread wires F7 to it.
- notice for MXSB thread: build the reference loader above.
- notice for assets thread: add the PATH trace event in mx_vfs_compat.h read_all/write_all so the golden trace hash is preserved.
- 2026-11-05 lead: kernel PR #4 (phase2a, fd1f15b) fixes three real flaws found in user-posted reviews of main (host-exit cap check, shared user memory, mint-before-check allocation) with tests; 45 in-guest checks. Heap reclamation open. Phase 2b target: two isolated apps, one workload, wrong-cap and exhaustion tests. Awaits user merge.
- 2026-11-05 lead: codegen delivered within scope (matrixos/codegen/: MXSB emitter+validator V1..V25, tiers with CPUID dispatch, lifter hook over clang AST; 40k checks). All six wave 1 modules are now delivered. Candidate 6 for acceptance.
- notice for tests thread: accept matrixos/codegen/ next (make test in folder). Its mxsb/ validator may serve as the F7 loader stand-in until the MXSB thread's reference loader exists; both must agree.
- notice for MXSB thread: codegen reports contract gaps: load-address layout rule missing, inter-section padding unauthenticated, v1 header is 40 bytes not 32. Fix in the contract (minor bump) and in the reference loader.
- notice for frontend thread: take tier flags from codegen's MX_TIER_MARCH[] in matrixos/codegen/tiers/mx_tiers.h rather than retyping them.
- notice for kernel thread: next sync should include matrixos/codegen/ once accepted.
- 2026-11-05 lead: trace-hash difference traced to the fuzzer swap, recorded as a documented integrated golden. Integrated build: golden 25/26, security_regressions 0 failures. F7 owner ruling is above (MXSB thread reference loader; codegen's validator as interim stand-in).
- 2026-11-05 lead: PR #4 merged with Phase 2a only; the three review fixes are PR #5 (review-fixes-bd1e914). main carries the host-exit flaw until #5 merges. User asked to merge #5.
- 2026-11-05 lead: PR #5 merged (main 00677c0). RULING for kernel thread: the user's "start Phase 2" covers all of Phase 2, so Phase 2b (two isolated apps, one workload, ELF loader, first Linux syscalls through the personality server) needs no further go; start it now. Sync request: fresh project-files-sync PR with everything accepted since #3 (frontend, canvas, superops, codegen, assets shim, tests, contract revisions).
- 2026-11-05 lead: user audit found a real mxcc bypass ("--" skipped the deny policy); fixed in frontend 1.0.1 with a live-plugin regression test. Harness weakness (missing compiler counted as pass) fixed.
- notice for canvas thread: the user's audit found an integer overflow in ghost_canvas.c with INT_MAX/INT_MIN coordinates. Fix with checked arithmetic and add a regression test; this is a finding you own.
- notice for tests thread: re-run frontend acceptance on 1.0.1 and add the "--" plugin case to security_regressions.sh F3; add the canvas overflow case as a new check.

# ALL PHASES IN PARALLEL (started 2026-11-05 07:35 UTC)

Lead thread for this round: "Control all phases in parallel". Board: "Track
bookkeeping for all phases" (control/STATUS.md). Everything above still
holds, except decision 4's sync owner, replaced by the rule below.

## REPO RULE (user, 2026-11-05 07:36 and 07:38)

The user ruled: bookkeeping holds the record of everything; the lead reads
bookkeeping's output and does all pull and push requests. So:

- Workers report every delivery and test result to the bookkeeping thread,
  which records it in control/STATUS.md (the record) and the decision log.
- The lead ("Control all phases in parallel") is the only thread that pushes
  to github.com/badwolfzm/pc, opens or edits PRs. It works from STATUS.md.
  This replaces decision 4's kernel-thread sync owner. No other thread runs
  `git push` or any GitHub write. Merging still needs the user's own word.
- Automatic means: every delivery recorded on the board becomes a branch and
  PR without waiting for the user. Nothing merges by itself.

Handoff format for kernel/crate work (so the lead can apply it):

1. Work on your branch name (table below) in your own clone, from main 00677c0.
2. When it is a candidate (acceptance rules below), put in project files
   `handoff/<branch>/`: `series/` from `git format-patch main..HEAD -o series/`
   and `HANDOFF.md` (what it is, checks run with counts, what is not verified,
   files touched, PR title, one-paragraph PR body). A single `.patch` for
   `git am` also works (the user-mapping thread used kernel/<branch>.patch).
3. Report it to bookkeeping, who puts the path and the check counts on the
   board. Do not message the lead; the lead reads the board.
4. The lead applies the series on a fresh branch from main, runs `cargo test`
   and `tests/kernel/run.sh` where the container can, pushes, opens the PR
   with the attribution block, and gives bookkeeping the PR number. A series
   that does not apply or fails comes back as a board note; fix and re-hand
   off with a `v2` suffix.
5. MatrixOS folders (thread 9 and the previous round's modules) are synced by
   the lead mirroring project files `matrixos/` and `control/` into a fresh
   `project-files-sync` branch and PR after each accepted delivery.
6. Dependencies between branches: a thread that needs another thread's
   change rebases on that thread's handoff series (apply it locally), and
   says so in HANDOFF.md, so the lead knows the merge order. The user asked for all phases to be implemented at the same time by
threads that work together as before. Rate limits apply to messages between
threads, so this file is the channel: read it before you start and again
before every delivery. Decisions written here by the lead are final until the
lead changes them.

## Hand-over from the previous round

- The old kernel thread ("Plan permissioned Linux-compatible kernel") stops
  pushing. The lead opens the pending project-files-sync PR itself. The old
  kernel thread does NOT start Phase 2b; thread 2 owns that. If the old
  thread has uncommitted Phase 2b work, it hands it off as
  `handoff/phase2b-from-kernel/` for thread 2 and reports to bookkeeping.
- main is 00677c0 (Phase 2a + review fixes). Every branch below starts from
  it. Open branches phase1-boot, phase2a-aspace-ipc, review-fixes-bd1e914 are
  merged history; nobody builds on them.

## Threads, branches, owned files (one owner per file)

| # | Thread | Branch | Owns (repo badwolfzm/pc unless noted) |
|---|---|---|---|
| 1 | Track bookkeeping for all phases | none | project files control/STATUS.md only. Reads everything, edits nothing else. |
| 2 | Build two isolated apps running together (Phase 2b) | `phase2b-isolated-apps` | `crates/warden-kernel/src/apps/` (new dir: the two guest apps, their loader glue), `crates/warden-kernel/src/tests_apps.rs`, `tests/kernel/apps/` |
| 3 | Add untyped memory and heap budget (Phase 3) | `phase3-untyped` | `crates/warden-kernel/src/untyped.rs` (new), `crates/warden-kernel/src/heap.rs`, `FrameAlloc` in `mm.rs` (move it to `frames.rs`, yours), `crates/warden-kernel/src/tests_untyped.rs`, `crates/warden-caps` changes to Untyped budgets |
| 4 | Run the Linux personality and ELF loader | `phase2b-linux-personality` | `crates/warden-kernel/src/elf.rs` (new), `crates/warden-kernel/src/linux_trap.rs` (new: `syscall` MSR setup and forwarding), `crates/warden-personality/` (new crate, the user-level server), `crates/warden-abi` (consumer changes), `crates/warden-kernel/src/tests_linux.rs`, `tests/kernel/linux/` |
| 5 | Budget IPC endpoint queues | `phase3-ipc-budgets` | `crates/warden-kernel/src/ipc.rs` (new: move `ipc()`, `Endpoint`, `Msg` out of `thread.rs`), `crates/warden-kernel/src/tests_ipc.rs` |
| 6 | Harden user memory mapping | `harden-user-mapping` | `AddressSpace` in `mm.rs` (map_user, load, unmap, W^X, frame zeroing, map-before-publish order), `crates/warden-kernel/src/tests_mm.rs` |
| 7 | Run Rust and QEMU tests on WSL2 | `test-infra-wsl2` | `tests/` (all run scripts), `HOST.md`, `crates/warden-kernel/src/tests.rs` (becomes the dispatcher), and the integration build `integration/all-phases` (kept locally on WSL2 from main plus every handoff series; results reported to bookkeeping; never pushed by thread 7) |
| 8 | Harden the Phase 0 supervisor | `phase0-hardening` | `crates/warden-sentry/`, `crates/warden-libgrant/`, `tests/escape/`, `tests/compat/` |
| 9 | Connect game runtime and rendering | none (project files) | project files `matrixos/runtime/` (new). Consumes canvas, superops, assets, RCS, frontend as built; never edits their folders. Synced by thread 7. |

Shared kernel files nobody owns outright, and the only edits allowed in them:

- `main.rs`: add exactly one `mod <yours>;` line, alphabetical. Nothing else.
- `thread.rs`: add one match arm in `syscall()` for your reserved range that
  calls `<yours>::syscall(k, f)`; add at most one field to `Kernel`, named
  after your module. `Thread` struct changes go through the lead (write the
  request here under "Requests").
- `arch.rs`: owned by thread 4 for the `syscall`-instruction entry only; any
  other change is a request.
- `Cargo.toml` (workspace): thread 4 adds `crates/warden-personality` to
  members. Nobody else touches it.
- `warden-caps`: `ObjectKind` already has Untyped, AddressSpace, Frame,
  Thread, Endpoint, Notification, SchedContext. Do not add variants; use
  these. Rights bits stay 5 (0x1F). Thread 3 owns budget code, thread 5 may
  add endpoint queue fields, thread 6 may add Frame attributes. Each in its
  own `impl` block or file so merges stay clean.

## Syscall number reservations (int 0x80, rax)

    0..9    taken (Phase 1/2a)
    10..19  thread 3  untyped: retype-to-frame, retype-to-aspace, free/reclaim, budget query
    20..29  thread 5  IPC: call, reply, non-blocking send, notify, endpoint queue budget
    30..39  thread 4  Linux personality: bind-personality, spawn-elf, forward/return
    40..49  thread 6  mapping: map frame, unmap, protect (W^X enforced), aspace query
    50..59  thread 2  apps: reserved, use only if the two apps need a kernel call the others do not give
    60..69  free (ask the lead)

An unknown number is still `Death::BadSyscall` (or whatever the current
behaviour is); do not change that.

## Contracts before code (due within the first hour, in project files control/contracts/)

- `mm.md`: thread 3 writes "objects" (what an Untyped is, retype rules,
  budgets, reclamation on thread death, heap growth cap); thread 6 writes
  "mapping" (map/unmap/protect semantics, W^X rule: no page both writable and
  executable, zero-on-map, frame published only after mapping is complete,
  TLB rule). Same file, two sections, each owner writes only their section.
- `ipc.md`: thread 5. Queue budget per endpoint, who pays (sender's
  SchedContext or endpoint owner), call/reply pairing, badge rules, what
  happens when the queue is full (block vs refuse: default refuse, since
  escape resistance beats convenience).
- `personality.md`: thread 4. How a `syscall` instruction in a bound thread is
  forwarded to the personality server over IPC, the register/word layout, the
  ELF loader limits (static, non-PIE first, PT_LOAD only, W^X at load), the
  first Linux syscalls (write, read, openat via fs.read, socket/bind/listen/
  accept via net.listen, epoll_create/ctl/wait, exit_group, mmap anonymous,
  brk) and which `warden-abi` class each one is.
- `apps.md`: thread 2. The two apps, what each may hold (capabilities), the
  shared service between them, the wrong-cap and exhaustion tests, and the
  user-visible proof (serial transcript) that one cannot read the other.
- `phase0.md`: thread 8. Which of the Known Phase 0 limits it closes (path
  races via supervisor-performed metadata/rename on fds, UDP so DNS works,
  ioctl per-request allowlist) and which it leaves to the kernel (per-library
  compartments: kernel Phase 3, not Phase 0).
- `runtime.md`: thread 9. The frame loop, which permissions the runtime
  requests (`render`, `superop=<name>`, `clock=coarse`, `random`, fs.read on
  the asset root), how it runs under `warden run` today, and the 30 fps budget
  split using matrixos/bench/pc numbers.

Threads 1 and 7 write no contract. Thread 7 writes `tests/README.md` with
the integration procedure instead.

## Dependencies and how to not wait on each other

- Thread 2 needs an ELF loader (thread 4). Until elf.rs lands, build the two
  apps as in-image blobs with the existing `AddressSpace::load`, exactly as
  the Phase 1/2a tests do. Switch to `elf::load` when it lands; keep both
  paths behind a `cfg` for one round so neither branch blocks the other.
- Thread 4 needs frame mapping (thread 6) and untyped retype (thread 3) for
  mmap/brk. Until then: static ELF with a fixed heap frame from the existing
  allocator; mmap/brk return ENOMEM beyond it and the test says "not verified".
- Thread 5 needs SchedContext accounting for budgets (thread 3). Until then:
  per-endpoint fixed queue depth (default 4), charged to the endpoint owner.
- Thread 6 moves nothing out of mm.rs except `FrameAlloc` (thread 3 takes it
  to frames.rs). Coordinate the move in the first hour: thread 3 does the
  move in its first commit and writes the commit hash here; thread 6 rebases
  on that commit (not on the branch) and nothing else.
- Thread 7 keeps `integration/all-phases` = main + every open phase branch,
  merged in the order 3, 6, 5, 4, 2, 8, with the kernel suite and `cargo test`
  run on WSL2 with KVM. A branch that breaks the integration build gets a
  notice here within the hour, and its owner fixes it; thread 7 never edits
  another thread's files to make it pass.
- Thread 9 works entirely in project files against the accepted MatrixOS
  modules and Phase 0 `warden run`; it needs no kernel thread.

## Acceptance (unchanged in spirit)

A branch is a candidate when: `cargo test` and `tests/kernel/run.sh` pass on
the branch alone (ALL HELD), every new check appears in the in-guest count,
its contract matches its code, its README section says what is verified and
what is not, and `git diff --stat main` touches only owned files plus the
allowed shared-file lines. The owner hands off (REPO RULE) and reports to
bookkeeping; the lead opens the PR against main (title "<Phase>: <what>"). Thread 7 runs the integrated suite and writes the result here. Nobody
merges: the user merges, or says "merge", in their own words.

## Requests (append; the lead answers inline)

(none yet)

## Decision log, this round

- 2026-11-05 07:50 lead: nine threads started; ownership, branches, syscall
  ranges, contracts and dependency fallbacks above. Sync-owner role moves to
  thread 7 after the pending sync PR. Phase 2b moves from the old kernel
  thread to thread 2. Rulings: queue-full refuses rather than blocks; W^X is
  mandatory for every user mapping including ELF load; the `syscall`
  instruction stays fatal for a thread with no personality binding (the Phase
  1 hostile check keeps its meaning); avx512 stays build-only.
- 2026-11-05 07:42 lead: REPO RULE (07:36, 07:38) written in; handoff format
  above. Bookkeeping: keep deliveries on STATUS.md and append only to the
  decision log here; the planning sections are the lead's. Lead now applies
  the user-mapping patch and opens the pending project-files sync PR.
- 2026-11-05 07:38 bookkeeping (user ruling): the bookkeeping thread holds the record of everything. Workers report every delivery and test result to bookkeeping, which writes them into STATUS.md and CONTROL.md. The control thread reads that output and performs all pull/push requests to badwolfzm/pc; it is the sole git pusher. Worker threads do not open PRs or push. This supersedes decision 4's "kernel thread is the single sync owner": flow is now workers -> bookkeeping (record) -> control (push). Live board: control/STATUS.md.
- 2026-11-05 07:40 user-mapping thread: DELIVERED (not pushed; user rule 07:36 says only the control thread pushes). Patch against main 00677c0: kernel/harden-user-mapping.patch (git am), full files also at kernel/src-harden-user-mapping/. Fixes the three items of the user's third review: (1) map_user checks the VA is free before allocating a frame, so a refused remap takes nothing; (2) W^X: map_user/load take mm::Perm {Code = R-X, Data = RW+NX, RoData = R+NX}; user code is read-only, data and stacks are NX; (3) tests: 14 kernel-side mm checks at boot plus 4 ring-3 cases (write own code, exec data page, exec stack all #PF; a legit thread still reads code and writes data). Verified: tests/kernel/run.sh ALL HELD, 63 checks, 0 failed, QEMU 8.2 TCG in the cloud container; not run on the user's PC.
- notice for Phase 2b threads (two isolated apps, ELF loader/personality, untyped memory, IPC budget): API change in crates/warden-kernel/src/mm.rs. `map_user(fa, va, perm: Perm)` and `load(fa, va, bytes, perm: Perm)` replace the `writable: bool` argument; `AddressSpace::pte(va)` is new. An ELF loader must map PT_LOAD segments as Perm::Code when the flags are R+X, Perm::Data for RW, Perm::RoData for R; refuse segments that ask for W+X and refuse segments that overlap an already mapped page (map_user returns None without allocating). Rebase on the patch above once the control thread merges it.
- 2026-11-05 lead: canvas fixed the audit's overflow and unbounded-line findings (64-bit clipping, bounded Bresenham), regression test added, golden unchanged. Both audit findings now fixed; tests thread to re-accept frontend 1.0.1 and canvas.
- 2026-11-05 lead: kernel PR #6 merged (main 0db603c, 53 in-guest checks, W^X user mappings, fixed endpoint queues). Phase 2b started on a new branch. ABI unchanged; kernel will announce before warden-abi.json changes.
