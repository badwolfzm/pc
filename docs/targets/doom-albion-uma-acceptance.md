# Doom → Albion: one-core, 512 MiB RAM, shared-GPU acceptance

Task: [#194](https://github.com/badwolfzm/pc/issues/194). Base: `4f05f4421b2602d5e7884770b90c440733a14cbc`. This is a **target and measurement contract**, not a claim of game compatibility. Complements the [ordinary-PC vision](../ordinary-pc-game-512mb-vision.md), [game roadmap](../game-compatibility-roadmap.md), and #146 report validator. Do not override security boundaries to meet an FPS target.

## Exact operating envelope

* **CPU**: one *physical core equivalent* available to the entire Warden guest, including game, personality server, storage, graphics and kernel. Launch QEMU with `-smp 1` and independently constrain/measure host CPU use and affinity if claiming one host core. A **single process** is not a synonym for one core: a process can have many threads; one vCPU can host many processes. Record processes, runnable threads, vCPUs and host core usage separately. One game process is an aspirational packaging constraint; service processes, if present, must be disclosed.
* **Guest physical RAM**: <= 512 MiB = 536,870,912 bytes **for the whole Warden guest**, not merely the game heap. Include kernel, page tables, caches, personality/server, software renderer, buffers, stacks and guest-resident compressed assets. No ballooning beyond cap.
* **GPU / integrated graphics**: at most 512 MiB of GPU-addressable **shared memory**; it is *not* a second free 512 MiB RAM pool on a UMA/integrated GPU. For a strict UMA envelope, require both `R_total <= 512 MiB` and `G_shared <= 512 MiB`, with GPU-occupied physical pages counted **once** inside `R_total`. Track guest-resident GPU surfaces, backing allocations and staging copies as disjoint physically allocated pages; aliases do not create extra bytes, real copies do. GPU allocations outside the capped guest must be separately reported and disqualify an unqualified whole-system 512 MiB claim.
* **Storage**: paging and preprocessing may use only authorized, durable or ephemeral storage, with sizes, bandwidth, latency, writes and integrity checks reported. Storage is not RAM; memory-mapped pages count when resident. Streaming/preprocessing must not alter the unmodified Albion executable or bypass authentication/anti-cheat policy.

Example strict UMA accounting in MiB: kernel+services 48, game resident pages 240, GPU-exclusive physical pages 96, read cache 64, other 32 = **480 MiB**. This is an *illustrative budget*, not measured Warden consumption. Shared pages mapped by both CPU and GPU appear in only one mutually exclusive bucket. A discrete GPU with dedicated VRAM is a different hardware class and must not be reported as integrated/shared RAM.

## Evidence levels (never substitute one for another)

1. **Host-only prototype**: compiler/linker, software renderer and stand-in WAD tests; clearly marked non-guest.
2. **Native synthetic guest**: real Warden QEMU boot and deterministic interactive frames from a synthetic probe. Does not prove Doom.
3. **Native Doom engine**: pinned genuine Doom engine source runs a real game loop under Warden with lawfully provided game data, input and render output. Partial `ET_REL` linking, missing imports, host mocks or a guest that merely prints success **do not** satisfy this level.
4. **Unmodified Albion client**: the authorized original Linux client launches *inside* Warden, authenticates through permitted services, renders changing gameplay frames, processes input and maintains a session without executable patching. All missing ABI, loader, graphics, networking and anti-cheat dependencies must be resolved legitimately; server policy may make this infeasible.

Each level is tested independently; an intermediate pass cannot be promoted to a higher level. Doom is the first runtime integration milestone; Albion is a longer-term stretch goal, particularly under 512 MiB.

## Reproducible measurement gate

Record exact Warden SHA, game build and SHA-256, data licenses/provenance, kernel/guest image SHA-256, QEMU version and flags, CPU model, host OS, graphics device/driver, memory mode (UMA/discrete/software), storage device, build flags, logs and machine-readable output. Capture **at least two separate cold-start successful runs** per title, each spanning startup, input response, at least 300 changing, timestamped frames and clean exit. Supply a visible frame hash or image evidence and full crash/fault logs. Show peak and time-series guest physical allocations and GPU-backed physical page unique counts; host process RSS alone is inadequate. Include CPU utilization, median/P95 frame time, FPS distribution, frame drops, paging reads/writes and latency. Do not assert any particular FPS until measured.

Fail the claim if the program only compiles, a host mock generates frames, the framebuffer stays static, a requested native service is stubbed, a genuine game import stays unresolved, RAM accounting omits kernel/graphics/services, the GPU memory is double-counted as free space, storage is unauthorized, or the original executable is modified to make Albion run. Report failure and the blocking dependency instead of weakening checks.

## Integration order and owner handoff

* **Doom D1**: close actual freestanding imports through reviewed independent providers; preserve fail-closed host-file/process operations. Integrate genuine WAD reads, memory safety, loader, guest game loop and graphics; then instrument the resource envelope. Relevant existing work: #170, #180, #185, #187, #188.
* **Constrained Doom**: only after native Doom works, test `-smp 1`, 512 MiB guest, and shared graphics accounting with a real game loop and repeatable FPS. Existing budget research: #134, #141, #142, #145, #146, #148.
* **Albion feasibility**: first catalog exact original-client Linux ABI, dynamic dependencies, rendering APIs, shader/texture working set, server and anti-cheat constraints. No promise of meeting the cap. Assess legal/technical precomputation and demand paging; refuse fake compatibility via host passthrough.

Claim and handoff: owner = current ChatGPT session; owned path = this document only; tests = documentation link/contents review only. No live QEMU, Doom, Albion, 512 MiB or FPS testing was executed as part of this change. Independent technical review requested. Other agents retain ownership of their kernel, game, memory and validation paths.
