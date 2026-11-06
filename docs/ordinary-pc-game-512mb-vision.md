# Warden/MatrixOS: ordinary-modern-PC game compatibility at one core / 512 MiB

**User clarification (2026-11-06):** The long-term goal is to run **games that run on a modern ordinary computer**, **not specifically games requiring a gaming PC**. Think mainstream desktops/laptops with integrated graphics and general-purpose CPUs. This **does not** redefine the target to exclusively low-end or old games. Doom is an integration milestone; Sims and Albion Online are potential test cases, not the definition of the workload class.

This is an ambitious **research objective, not a claim of universal feasibility or current compatibility**. Individual games may be fundamentally incompatible with the resource envelope because of active memory footprint, CPU/GPU work, deadlines, proprietary platform/anti-cheat constraints or missing ABI support. Document failures precisely; do not claim that swap or preprocessing makes every title playable.

## Strict experimental target

- **Physical guest-machine RAM budget: 512 MiB total** (not merely per-process RSS): kernel, memory manager, runtime/personality, resident game pages, framebuffers, compression buffers, metadata, and paging overhead. Also report total host RSS separately.
- **One active guest CPU core / QEMU `-smp 1`**. Multiple guest threads may be time-sliced on that core. Report guest game process count, helper process count and host process/thread count independently. "One core" does **not** require one instruction stream or forbid threads.
- **No dedicated gaming GPU assumed.** Software rendering and/or ordinary integrated graphics are admissible, but record which backend is actually used. Do not count an unreported host GPU as free acceleration.
- **Local execution**, not cloud/remote offload. Suitable explicitly authorized persistent storage can back virtual pages, caches and verified preprocessing artifacts; the amount, bandwidth, write endurance, permissions and integrity of storage must be recorded.
- **Unmodified executable compatibility** where possible. Compatibility shims may be added to Warden, but no undocumented game modifications, access-control bypass, publisher authentication bypass or anti-cheat circumvention.
- **Correctness precedes FPS.** Very low FPS may be an acceptable exploratory result; track input latency, crashes, network timeouts, and frames rather than calling an application launch "playable."

## Reference population

Survey games known to run on ordinary up-to-date Windows/Linux desktops or laptops with integrated graphics (not merely high-end dedicated-GPU systems). Record per title: game/build version, source or owner-authorized installer, OS/API requirements, integrated GPU model, stock modern-PC RAM/CPU requirements, network/server requirements, licensing, hardware references, and reproducible baseline behavior. The title list is a **measured corpus**, not "all games" inferred from marketing.

Keep distinct groups: (a) offline 2D and lightweight 3D, (b) older full 3D, (c) current integrated-graphics 3D, and (d) online titles with strict timing/security requirements. Doom's source-port progress tests the kernel/runtime, but does not imply native Windows/modern Linux API compatibility.

## Research work packages, with falsifiable gates

1. **Compatibility inventory:** Static/dynamic ELF and Windows PE loader requirements, libc/runtime libraries, API/syscall/thread/TLS/futex/clock/filesystem/network/graphics/audio dependencies. Fail closed and report unsupported operations, rather than inventing success.
2. **Bounded memory virtualization:** Demand paging into a large *virtual* address space with the **512 MiB physical cap enforced**; precise page-fault and allocation-failure semantics; W^X, mapping identity, capability limits, swap provenance and recovery. No unsanctioned device probing. Compression and dedup must account for CPU and RAM overhead.
3. **Working-set characterization:** On consenting, lawful workloads, gather page references, unique hot pages per 16 ms/100 ms/1 s interval, reuse distance, page faults, writeback, compression ratio, swap IOPS and bytes. Compare an ideal trace-informed offline schedule with a realizable online predictor. An active set above budget is a documented performance impossibility for that interval, not a paging bug.
4. **Legal deterministic precompute:** With permitted assets, cache shader compilation, texture transforms, geometry preprocessing and authenticated derived artifacts on explicitly authorized storage. Runtime dynamic events, network-dependent game state and arbitrary frames must still execute. Cache versions, hashes, invalidation and licensing are part of the acceptance gate.
5. **Graphics fallback:** Measure software rendering at low native resolution on the single core; record genuine frame timing and graphics feature coverage. Integrated-GPU acceleration is an *alternative profile* and must not be conflated with all-CPU performance.
6. **Single-core scheduling:** Time-slice as many required guest threads as necessary on one vCPU while preserving correct synchronization and guest wall-clock semantics. Do not fake network timing or client/security responses.
7. **Adaptive governor:** Use actual telemetry to manage resident-page budget, compression, prefetch, shader-cache occupancy, render resolution and error recovery. Compare against static policies with repeatable input traces; if overhead outweighs gains, disable it.
8. **Acceptance and negative security cases:** Unauthorized swap source, corrupted/precomputed data, stale grants, mapped-pointer overflow, truncated asset reads, exhaustion, power-loss recovery and guest exit must fail safely. Request separate security review before sharing kernel or storage boundaries.

## Required evidence for each attempted title

- Exact executable hash and license/provenance; Warden, engine and integration commit SHAs.
- Host model and CPU, memory, graphics mode, storage model, QEMU/TCG/KVM and `-smp 1`; actual guest RAM cap and peak host RSS.
- Original ordinary-PC reference baseline (which OS, integrated GPU, RAM, FPS if known), independently measured where possible.
- Startup success/failure; game process/thread inventory; missing API report; loaded assets; peak resident pages, compressed bytes, swapped bytes, page-fault p50/p95/p99 and I/O stalls.
- At least two repeatable boots and, if frames exist, 300 frame timestamps/digests with actual FPS/frame-time percentiles; track whether input works and game state advances.
- Outcome labels: `not_loaded`, `loaded_no_render`, `renders_unplayably`, `interactive`, `playable_by_declared_threshold`. Every threshold must be stated before testing. A host-side mock is never a native Warden result.

## Coordination / ownership

This document supplements [game-compatibility-roadmap.md](game-compatibility-roadmap.md) and earlier [game-storage-precompute-vision.md](game-storage-precompute-vision.md), not replacing their Doom-first milestones or the independent Warden-kernel requirement.

- **Task ID:** `ordinary-pc-512mb-game-vision-20261106`
- **Owner:** assistant contributor, explicitly instructed by repository owner to post this clarified goal to GitHub.
- **Base:** `7496c6a8e3d48b41445f1bc8fa24b4012cf26661` (inspected live main).
- **Owned path:** this new Markdown file only. No edits to existing claims in `control/`, `matrixos/`, kernel, or other agents' branches.
- **Dependencies:** issue #92 (integration vision), #95 (real Doom first), published game roadmap and storage vision; request lead/bookkeeping to incorporate the clarified *ordinary-modern-PC* target into future tests.
- **Validation:** repository file readback/Markdown review and link/path checks. **No code, benchmark or game execution is claimed by this documentation-only change.**
- **Merge:** independent review and existing coordination/approval rules apply; draft PR, no automatic merge.
