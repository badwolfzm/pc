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
