# Acceptance and handoff — storage-first precomputation plus qualified swap

**Companion to:** [game-storage-precompute-vision.md](game-storage-precompute-vision.md) · task [#76](https://github.com/badwolfzm/pc/issues/76). These gates specify *future required evidence*, not tests already passed.

## Progressive gates

| Gate | Demonstration | Required evidence / negative tests | Status |
| --- | --- | --- | --- |
| S0 inventory | Enumerate local and authorized remote storage providers without mounting, erasing or modifying unrelated data | Report capabilities, quotas, legal scope, backend types, disabled backend behavior; unauthorized path/device denied | NOT IMPLEMENTED |
| S1 content-store | Content-addressed immutable precomputed objects with versioned manifests on one local device | Golden content checks, corruption/missing/oversized object refusal; deterministic rebuild; cross-compartment data refusal | PROPOSED (some existing asset-store primitives, not integrated) |
| S2 portability | Same artifact API backed by SSD/HDD/USB/SD/removable and optionally network/object storage where writable and authorized | Per-provider success/skipped reason, unplug, full device, timeout, revoked grant, stale version, changed device identity, read-only denial; no claims for devices never tested | NOT IMPLEMENTED |
| S3 preprocessing pipeline | Compile/bake/index all *safe invariant* candidate work, including code, static graphics, assets, audio, resource lookup and reusable numerical tasks | Cold-preprocess time, output sizes, provenance, warm/cold startup, cache hit ratio, deterministic output and unchanged runtime semantics | NOT IMPLEMENTED |
| S4 resource scheduling | Bounded tiered cache, predictable eviction/prefetch and sustainable wear budgets | Workload benchmarks on actual host and one-vCPU runtime, disk read/write/IOPS, p95/p99 stall time, endurance/space accounting and cleanup | NOT IMPLEMENTED |
| M0 qualified swap | Safe anonymous-page swap on one suitable local backend, no existing workload regression | Paging correctness, zeroing, ownership, faults, exhaustion, crash/disconnect and corruption; pinned pager/critical I/O paths | NOT IMPLEMENTED |
| M1 multi-provider swap | Opt-in safe swap across separately approved page-capable devices | Verified device properties, file extent stability, fault-progress under pressure; failure containment, metadata integrity, encryption policy | NOT IMPLEMENTED |
| R0 Warden G0 | Real Warden native Doom-like raster guest, not a host program | Two QEMU boots + clean exits, `-smp 1`, checked frame hashes, >=300 timed frames each, total physical guest and host memory, isolation tests | NOT VERIFIED |
| R1 real Doom | Playable open-source Doom-compatible engine with lawful assets on one vCPU | Source/binary+WAD hashes/licenses, input/video/audio (as required), screenshot/frame proof, failure recovery, cold/warm/precomputed comparison | NOT VERIFIED |
| R2 Albion inventory | Authorized dependency and vendor requirements audit | EULA/anti-cheat check, actual client and dependencies, performance constraints, no bypass | NOT VERIFIED |
| R3 Albion launch | Only if permitted: real unchanged client starts, draws frames, accepts input/network login | Explicit vendor-permitted environment and successful login without bypass; measured single-vCPU FPS/frametime/RAM/disk and limitations | NOT VERIFIED |

## Comparisons and metrics (report real data, never invent)

For a fixed game/version/asset pack and input workload, run **no-cache cold**, **preprocessed cold**, **preprocessed warm**, and **memory-constrained swap-off/on** configurations on the **same** recorded hardware. Include total preprocessing CPU-seconds, peak RAM and storage bytes, cache reuse/hit rates, I/O operations, read/write amplification, average/median/p95/p99 frametime, startup time, total vCPU utilization, swap-ins/outs, major faults, frame hashes, reproducibility, provider health, storage wear and crashes. Differentiate offline precomputation resources from the one-vCPU runtime constraint. Use at least two separate full guest boot/clean-exit runs for G0/G1 claims.

Do not infer a speedup from compute-only throughput, or an improvement from higher swap capacity; precomputation is profitable only if the measured total cost, latency and storage tradeoff are acceptable. Workload correctness and no denied-capability bypass are first-order acceptance requirements. A result cached from an earlier authorization context must be reauthorized, not blindly reused.

## Security and failure tests

Must cover: untrusted device mount/path; symlink/path traversal; revocation after scheduling I/O; swapped slot reused by another guest; stale generations, double-free, corrupted manifests/data; interrupted writes; device unplug or remote outage; crash during swap-out/in; full quota; block alignment and checked integer multiplication; unbounded recursion into filesystem/network from page-fault handler; deadlock during memory pressure; cache poisoning, version and CPU-feature mismatch; absent signature or forbidden license; SSD wear limits and removable storage identity changes. Denials must not leak data or grant privileges; rollback must not corrupt unrelated content.

**Hard exclusions:** no writable swap on read-only/sequential-only media lacking suitable random access; no swap/asset cache access to unauthorized devices; no use of existing Linux kernel as the implementation; no fabricated frame or online state; no authentication/anti-cheat bypass; no claim that a preprocessed artifact can substitute for arbitrary runtime world changes.

## Agents and integration handoffs

- **Control/bookkeeping:** record issue #76 and reconcile these new docs with [PR #63](https://github.com/badwolfzm/pc/pull/63), leaving the owner of that roadmap responsible for modifications.
- **Storage/asset owner:** evaluate metadata-rich content store backend interface and tiered storage; current `matrixos/assets/` is an immutable asset streaming prototype, not virtual swap.
- **Kernel/memory owner:** design page fault/physical-frame/quota/pinning/reclaim and swappable mappings; obtain security lead approval for every ABI or capability change.
- **Graphics/compiler owner:** identify measurable CPU work that can be baked into versioned artifacts and replayed safely, compatible with `matrixos/contracts/gpu-commands.md`; validate output against uncached reference.
- **Linux ABI/game integration owner:** pin real Doom engine and legal WADs; compare runtime behavior and compatibility in Warden; inventory Albion only with authorization.
- **Security/reviewer:** independently inspect manifest trust, device authority, confidentiality, fault progress and crash consistency; check no privilege expansion.

**Handoff record:** owner ChatGPT contributor; task #76; base `b2eb17f1c5c0e57a57f6c672c793cc3638d0c2a9`; branch `agent/game-storage-precompute-vision-20261105`; owned paths `docs/game-storage-precompute-vision.md` and `docs/game-storage-precompute-acceptance.md`. Documentation only; no kernel code touched. Validation: inspect Markdown and GitHub diff/readback; runtime tests not applicable. Independent review is required before merge. No implementation claim or benchmark has been made.
