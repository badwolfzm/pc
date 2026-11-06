# One-core / shared-GPU 512 MiB acceptance contract (Doom first; Albion later)

Task: [#190](https://github.com/badwolfzm/pc/issues/190). This is a proposed **test contract**, not evidence that the games boot. Complements [ordinary-PC vision](ordinary-pc-game-512mb-vision.md), [game roadmap](game-compatibility-roadmap.md), and [512 MiB consolidation #155](https://github.com/badwolfzm/pc/issues/155). The canonical runtime validator/schema remains the responsibility of #155 and PR #146's reviewers; this document neither forks that implementation nor changes its acceptance logic.

## What the requested resources mean

- **Compute:** exactly one *active guest vCPU*, e.g. QEMU `-smp 1`; multi-threaded guest applications are allowed but must time-slice on that vCPU. Record guest processes, guest runnable threads, QEMU host processes and host threads separately. A single host *process* alone does not limit host CPU usage: capture host CPU affinity/usage and accelerator threads if claiming strict single-physical-core execution. Do not force Albion's process tree into one process or fake synchronization.
- **System memory:** `guest_physical_total <= 512 MiB`, including Warden kernel, personality/services, game code/data, resident heap/stack, memory metadata, software framebuffers, shared graphics buffers, file cache, driver/pager bookkeeping and compression buffers. Count actual **unique physical pages**, not sums of virtual address mappings. The value is a machine-level envelope, not merely the game's RSS.
- **Shared GPU memory:** `shared_gpu_resident <= 512 MiB` is a *subset* of the total 512 MiB when the GPU uses UMA/integrated graphics with guest RAM as backing. The shared GPU maximum is **not an additional 512 MiB of physical memory**. The complete machine cannot have 512 MiB of guest-private resident RAM *plus* 512 MiB of guest-backed GPU pages under this profile.
- **Graphics accounting:** for real integrated GPU passthrough/virtualized acceleration, distinguish GPU-owned dedicated/stolen memory, mapped system pages, pinned DMA pages, external host VRAM and opaque driver allocations. A hypervisor exposing a framebuffer does not prove physical iGPU acceleration. If the host allocates extra graphics backing outside guest physical RAM, separately report it and mark the strict *whole-machine* 512 MiB claim unproven. Software-rendering guest surfaces count in total resident RAM even if the GPU-resident figure is zero.
- **Storage:** authorized paging/preprocessing on persistent storage is allowed and must be accounted as storage, but resident page cache, swap tables, I/O buffers, encrypted metadata and compression state count against 512 MiB. No remote rendering/cloud offload. Page faults and storage stalls must be measured. Extra virtual address space is not extra RAM.

All values are MiB (2^20 bytes); measure page counts times page size, and record rounding, allocation granularity, and whether host-side bookkeeping is included. **Report peak simultaneous use**, not a sum of unrelated per-subsystem maxima. For safety, also provide conservatively summed per-category peaks as an upper bound, but do not label it measured peak concurrency.

## Memory identity and invariants

Let `P(t)` be the set of unique guest physical page frame identifiers resident at time `t`, and `G(t)` the subset backed by those page frames and mapped/pinned for graphics.

```text
guest_ram_used(t) = |P(t)| * PAGE_SIZE
shared_gpu_guest_pages(t) = |G(t)| * PAGE_SIZE
G(t) subset-of P(t)
guest_ram_used(t) <= 512 MiB
shared_gpu_guest_pages(t) <= 512 MiB
```

A graphics buffer mapped simultaneously by a game and driver is **one physical allocation** (not two). A 512 MiB address aperture is a *capacity* and is not evidence of 512 MiB resident GPU allocations. If `G` cannot be identified or host-side shared allocations are opaque, report the metrics as *unknown* instead of treating them as zero.

**Example A (fits, illustrative not measured):** kernel/services 48 MiB, game-private pages 260 MiB, graphics-shared pages 96 MiB, cache/pager 52 MiB, miscellaneous 20 MiB = **476 MiB unique guest physical RAM**. Shared GPU figure = 96 MiB (already included); 36 MiB headroom.

**Example B (does not fit):** kernel/services 48 MiB, game-private pages 360 MiB, graphics-shared pages 128 MiB, cache/pager 32 MiB = **568 MiB**, exceeding the cap by 56 MiB even though the GPU-shared allocation alone is <512 MiB.

**Example C (double-count trap):** the same 64 MiB physically backed texture is mapped twice in the game address space and once into the graphics service. Unique resident impact = 64 MiB, **not** 192 MiB. Require a page-identity or independently justified deduplication method; absent that, give an uncertainty bound and do not certify compliance.

Reservations/ballooned pages, compressed pages, swap slots, pinned host DMA allocations, driver carve-outs and MMIO apertures must each be disclosed with measured vs configured status. If physical host resources are used beyond the guest contract, report both *guest-only* and *all-observed-host* profiles, never collapse them.

## Suggested title-specific gates (do not imply capability)

| Step | Evidence | Fails if |
| --- | --- | --- |
| G0/G1 | deterministic Warden raster/interactive loop with one vCPU; frame hashes, real input, two separate native boots | frame mocks presented as guest video, frame count without advancement |
| G2 | real pinned Doom-compatible engine ported to freestanding Warden plus legally supplied Doom/Freedoom data; graphics, input, clean guest exit | only host mocks or relocatable engine object exists |
| G3 | **unmodified** Linux Doom executable with actual ELF/linker, libc, device and syscall handling | missing dependencies silently stubbed |
| G4 | authorized Albion Online client static compatibility inventory: specific build hash, architecture, loader, DLL/SO dependencies, threading, 3D, network and launch requirements | unavailable or unauthorized client, guessed API support |
| G5 | vendor-permitted unmodified Albion client in native Warden, real render and input, then login/server and stability tests without bypasses | login/anti-cheat circumvention, remote GPU offload disguised as local |

At every gate, require a **separate** one-vCPU/512 MiB experiment. A pass at G2 is no evidence that G5 is feasible. Run software-only graphics and integrated-GPU graphics as distinct profiles; report the actual backend. Low FPS is permissible as exploratory output; responsiveness, deadline failures and server timeouts still matter.

## Proposed evidence to feed the canonical #155 gate

1. Exact kernel, tools, game, asset and CI run hashes; game license/permission; tester, execution environment, CPU model and GPU model.
2. QEMU invocation and virtualization mode, configured `-smp 1 -m 512M`, plus **observed** vCPU run time, guest-process/thread inventory, host process/thread inventory, process CPU affinity and CPU usage.
3. Synchronized time series: unique guest resident pages; graphics-shared page IDs (or clear unknown flag); driver/stolen/pinned/shared host allocations; software surfaces; guest RAM cap; peak host RSS, page cache, swap usage and external dedicated GPU VRAM.
4. Frame timestamps/digests for >=300 *advancing* frames, real input/state changes, frame-time p50/p95/p99, achieved FPS, exits/crashes and error histogram, from **two independent boots**. Mark no-render failures rather than fabricating FPS.
5. Paging trace provenance, authorization and I/O volume/latency; compare online pager with offline optimal only as an oracle, not a realizable scheduler.
6. Explicit verdicts: `guest_ram_cap_pass`, `shared_gpu_subbudget_pass`, `host_memory_observed`, `single_vcpu_pass`, `single_host_core_evidence`, `real_guest_render_pass`. Each can be pass/fail/unknown, with source/evidence reference. Unknown is never pass.

## Security and feasibility constraints

Never weaken capability validation, guest isolation, DMA/IOMMU safety or syscall mediation to increase FPS. Unknown graphics memory ownership must fail closed for a *strict envelope* claim, not automatically block useful exploratory tests. CPU, GPU and I/O bandwidth are independent constraints: storage precompute or swap cannot eliminate a game's irreducible live working set, runtime shaders or server timing. Proprietary game licensing, authentication and anti-cheat are hard boundaries, not bypass goals.

## Handoff

- Owner: current user-authorized ChatGPT session. Base: `4f05f4421b2602d5e7884770b90c440733a14cbc`. Claimed path: this NEW file only; branch `docs/game-shared-vram-one-core-20261106`.
- To #155 / game evidence owners: reconcile the unique-page and shared-GPU metadata with the canonical acceptance tool (#146 preferred there), **without** duplicating validators. To Doom G2 owners: keep actual guest rendering distinct from host Doom compile/link success. To Albion compatibility owner: verify actual authorized client requirements before estimating the 512 MiB feasibility.
- Validation: documentation review, exact arithmetic examples, GitHub readback. No QEMU, binary load, genuine game execution, FPS or memory benchmark performed by this contribution. Independent review and lead merge gate remain required.
