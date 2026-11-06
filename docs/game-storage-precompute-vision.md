# Warden / MatrixOS — definitive low-resource game vision: storage-first precomputation

**Status: target architecture; not implemented.** Requested by user 2026-11-05. Claim [#76](https://github.com/badwolfzm/pc/issues/76). Complements [game milestones PR #63](https://github.com/badwolfzm/pc/pull/63); does not edit the other agent's roadmap.

## Mission and hard requirements

Build an independent x86-64 Warden/MatrixOS microkernel, **not a Linux kernel**, that first runs actual **Doom**, then attempts legally and vendor-permitted **Albion Online**, even with low frame rate and limited RAM. Optimize the execution path for **one guest game process, one vCPU**, bounded total system RAM, capability isolation and correct output. Keep game-service, storage, graphics, input and ABI personalities in separately authorized compartments if needed. One vCPU does not imply one QEMU host thread or that all Warden services are part of the game process.

**Central design principle: precompute *everything that can correctly and safely be precomputed*, persist and retrieve those results from *any technically plausible, authorized storage device*, and perform only genuinely dynamic work in real time.** This applies across the compiler, loader, virtual memory, graphics, data, assets, audio, application startup and other reusable computations. It is not merely a swap-file design or a GPU-only cache. Not everything is precomputable: actual inputs, unpredicted physics, arbitrary live player actions, network responses, authentication, changing worlds and anti-cheat interactions remain runtime tasks. Precomputation must not substitute guessed data for correctness.

Security, integrity, explicit resource quotas, fault isolation and legal/vendor compliance have priority over FPS and launch success. Do not bypass anti-cheat, authentication, signatures or other protections, and do not repackage copyrighted game assets without authorization.

## Storage: universal *capability discovery*, not an impossible universal compatibility claim

The user wants all plausible, authorized storage to be eligible as a **persistent precomputed-result store**, and, if writeable and suitable, optionally a **swap/page backing store**. Candidate classes: NVMe/SATA SSDs; HDDs; USB flash/external drives; SD/microSD cards; removable disks; supported network storage; authenticated remote object/blob stores; approved flash devices; RAM caches; and read-only optical media as sources for prebuilt artifacts. Sequential tape/media can hold bulk archives or offline caches but cannot be treated like fast random-access swap. Optical/ROM media and read-only mounts cannot accept new writes. RAM disks do not increase available physical RAM.

All device access is opt-in, explicitly granted by capability and scoped to a named directory, object namespace, file/partition or block range. Enumerating devices **never** grants write, formatting, erasing or unrestricted reads. No raw-device access without a specific, reviewed user grant. Nonlocal stores require transport authentication, secrecy/integrity policy, offline and outage behavior. Handle insertion/removal and varying capacity without privileged host filesystem access or weakening isolation.

Proposed **storage broker** interface (design, not an existing API):
- Discovery returns backend type, identity, authorization scope, volatility, read/write/append, random-access, atomicity, alignment, capacity/quota, latency/throughput estimate, durability/endurance, removability and health.
- Operations: reserve namespace, write immutable content-addressed object, fetch/verify object, atomically publish manifest, enumerate metadata, pin/unpin, evict and revoke. Optional `read_page/write_page/flush` only on page-safe backends.
- Never write data before selecting an authorized backend and checking free space, quota, expected wear and recovery policy. Use stable object hashes, schema versions, deterministic build IDs, input hashes and signed/authenticated manifests where required. Privacy-sensitive cached data must be encrypted under an explicit threat model.
- Persistent results must include full provenance (input content hashes, compiler/kernel/game build version, options, CPU features, asset authorization, dependency versions, security domain, freshness policy and compatibility checks). On mismatch/corruption/stale security context: reject and recompute or fail gracefully, never silently use an incompatible cached result.
- Support a tiered cache: hot RAM -> approved local SSD/HDD/removable store -> authorized slower/remote archive, with asynchronous retrieval, quotas, priority and explicit error handling. Offline-first where practical, but no requirement to fetch remotely merely to draw a frame.

## What to precompute: aggressively, but only when semantically valid

1. **Executable build / loading:** compiler optimization, validated MXSB/ELF metadata, relocatable or pre-linked artifacts where format/ASLR/permissions allow, verified code/data pages, dependency graphs, compatible CPU-feature dispatch, startup indexing and deterministic generated tables. Do not cache privileged approvals or trust decisions without revalidation; do not bypass runtime relocation, ASLR, signatures or protected launch.
2. **Graphics and pseudo-GPU:** offline scene/static geometry baking, visibility/portal/BSP/PVS where geometry is static, mipmaps/texture transcodes, texture atlases, precomputed fixed-point tables, lookup tables, lighting baked for static elements, mesh/tile bins, occlusion/LOD indices, software-raster kernels, sprites/animation frame preparation where lawful, reusable shaders/intermediate programs and render command templates. At runtime evaluate current camera, visible moving actors, live effects, view-dependent results, input and frame composition. Precomputed graphics are reusable data, **not** a real GPU replacement for all 3D rendering.
3. **Resource and I/O planning:** signed manifests, indexed content-addressed assets, file metadata, compressed chunk dictionaries, deduplication, streaming priority hints, predicted asset neighborhoods from safe static maps, cache warming, prepared decompression blocks and fault-free bundles. Existing `matrixos/assets/` has read-only content-addressed 64 KiB chunks, mipchains, nonblocking residency; integrate by proposal, not by unauthorized edits.
4. **Audio/media:** stable sound decoding, resampling kernels, converted sample banks, sound indexes and static mixes. Runtime mixing, positional effects and variable streams still require work.
5. **Computation/data:** constant tables, deterministic simulation subresults for strictly invariant inputs, collision/navigation acceleration structures for static geometry, verified immutable program data, reusable data transformations and safe numerical approximations with explicit error bounds. No pretending dynamic world/player state is static.
6. **Networking/online applications:** cache only vendor-permitted, cacheable static resources and ordinary DNS/config metadata under expiry and authorization rules. Live account/session state, messages, market data, competitive state, authoritative simulation, security tokens and anti-cheat must never be forged, replayed outside protocol or precomputed.
7. **Memory management:** prefetch immutable file-backed pages, prepared executable/asset bundles, free clean caches under memory pressure, then optional swap for dirty anonymous pages. A precomputed object store is distinct from VM swap.

**Precomputation scheduling:** choose work using expected saved runtime CPU/latency per stored byte, build-time cost, read latency, reuse rate and staleness risk. Build immutable artifacts asynchronously during *explicitly authorized* preparation phases (not hidden background work by this assistant); support resumable manifests/checkpoints, deduplicate by content hash, cap CPU/disk/wear cost, prioritize immediately needed results, allow partial results and avoid cold-start work that exceeds budget. Explicitly distinguish offline preprocessing on more CPUs from runtime one-vCPU measurements: if the aim limits preprocessing to the same single CPU too, measure that separately.

## Optional swap file / page-backed virtual memory

Support an optional swap region/file on **any authorized backend satisfying the pager's actual requirements**: stable random access, verified slot persistence semantics, controlled failure recovery, sufficient space, and nonblocking or bounded page-fault progress. Suitable SSD/HDD/removable block/file stores may qualify. A filesystem file must have proven-safe stable mappings or a correct filesystem-aware paging path; avoid sparse extents, unbounded filesystem recursion or unreviewed CoW changes. Network-backed swap is experimental, only if paging's network/dependency stack is wired and outage-deadlock safe. Tape and write-only/read-only devices cannot generally serve random-access dirty-page swap but remain eligible precomputation artifact repositories.

Pager design: per-compartment page/frame/slot accounting, bounded replacement (clock or measured alternative), dirty-page eviction and verified reload, mandatory pinned page-fault handler/pager/critical I/O stacks, emergency allocation, encrypted/authenticated data when required, discard of clean reconstructible pages before dirty pages, bounded queues and timeouts, generation checks and slot scrubbing on reuse. Device full/corrupt/disconnected/slow: block within a stated deadline, recover from another validated copy, or fail/kill only the affected guest safely. Never fabricate zero pages for corrupt saved dirty data. Swap does **not** increase physical RAM or ensure an acceptable frame rate; random page faults can devastate single-vCPU performance.

## CPU rendering, MatrixOS software GPU and precomputed frame pipeline

Use existing [RCS GPU command set](../matrixos/contracts/gpu-commands.md) as a reference: client-owned slots, copy-then-validate, bounded commands, explicit render capability and a software render service. No new unrestricted GPU permission. Use `matrixos/superops/`, `matrixos/canvas/` dirty tiles and `matrixos/assets/` streaming where integration is reviewed. Prepare scene artifacts offline; fetch with budgeted I/O ahead of use; hold a bounded hot-set; validate any GPU-like command and render changed geometry/tiles in software. Fall back to lower resolution/LOD, reduced update rate and frame skipping when needed. Never hold a page-fault-critical path on an evictable graphics asset or block on removable storage indefinitely.

A useful pipeline:

```
AUTHORIZED STORAGE(S) -> immutable precomputed objects, manifests, verified game assets
                   |                  |
                   v                  v
       tiered validated cache <-> optional qualified swap/page backing
                   |
     Warden game (1 process) --capabilities--> render/input/storage services
                   |                            |
          live state+commands       CPU software renderer / compositor
                   |                            |
                   +------------------------> frame output + measured timing
```

## Game milestones and interoperability

- **G0:** deterministic host-side game-like render primitives eventually run inside Warden/QEMU, not merely as host tests. One vCPU, bounded total memory, repeated output hashes.
- **G1:** interactive bounded native game, verified video/input and cleanup.
- **G2:** real open-source Doom-compatible source engine running in Warden with lawfully obtained WAD/data and a precomputed artifact pack; compare cold/warm runtime and memory.
- **G3:** optionally, unchanged Linux Doom executable through Warden's user-space Linux ABI personality and real loader/libc/syscall semantics. Keep it independent of G2.
- **G4:** authorized Albion Online inventory, required loader/runtime/3D/network/security and legal restrictions; determine if any client precomputation is vendor-permitted and beneficial.
- **G5:** only if allowed: unchanged vendor-permitted Albion client renders, accepts input and performs legitimate login/network operations on one vCPU with measured realistic FPS/RAM/storage use. **Albion may be infeasible** without proper graphics drivers, threading, launcher requirements or vendor support, irrespective of disk size.

## Cross-agent integration and truthfulness

Source of truth for permission names is `abi/warden-abi.json`. Existing graphics contract `matrixos/contracts/gpu-commands.md` and asset implementation `matrixos/assets/README.md` describe different layers. Request owner review before new capabilities, swap mappings, filesystem, renderer, loader, device access, ABI or kernel changes. Existing coordination: `AGENTS.md`, `control/CONTROL.md`, `control/STATUS.md`, `control/COORDINATION.md`. No files owned by other agents changed in this PR. Do not supersede PR #63 by silently editing it; link this goal as an expansion to be reconciled by its owner and the lead.

**Not verified:** no generic storage broker, pager/swap, cross-device persistent precomputation, Albion client launch, actual Doom execution, FPS or RAM improvement is established by this document. Do not mark implementation complete from docs alone.
