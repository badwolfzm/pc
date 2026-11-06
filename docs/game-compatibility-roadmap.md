# Game compatibility acceptance ladder — Doom first, Albion Online later

Task: [#62](https://github.com/badwolfzm/pc/issues/62) · Owner: independent ChatGPT contributor · base: `b2eb17f1c5c0e57a57f6c672c793cc3638d0c2a9` · 2026-11-05.

## User's target (not a claim of current capability)

Run a playable **Doom** and, as a longer-term stretch goal, **Albion Online**, inside Warden/MatrixOS even at limited FPS and constrained RAM, on **one CPU process**. The independent Warden microkernel and its mediated Linux ABI are architectural requirements. **Do not switch direction to using a Linux kernel.** Security isolation, bounded resources and correct output take priority over speed.

"One CPU process" is ambiguous: (a) one application process, (b) one host OS process containing a Warden VM, or (c) use no more than one CPU core / one vCPU. For a testable conservative target, measure all three separately: **one guest game process**, **one Warden/QEMU host process** (subject to QEMU helper threads), and **`-smp 1` one vCPU**. A multi-threaded renderer in one process is not necessarily single-core. No claim of strict one *host thread* is made.

## Verified repository starting points (source/docs, not rerun by this contribution)

* `README.md`: narrow x86-64 Linux syscall personality, static ELF, in-kernel address spaces, static BusyBox/HTTP examples. These are not evidence that Doom or Albion runs.
* `tools/linux-compat/preflight.py`: read-only ELF structural preflight; not a compatibility/execution verdict.
* `matrixos/gpusim-src/`, `matrixos/superops/`, `matrixos/runtime/`: software graphics and game runtime building blocks, not a complete SDL/GPU/desktop service.
* `docs/linux-container-acceptance.md`: no general dynamic linker, process tree, writable FS or commercial games; host-PC acceptance open.
* [#61](https://github.com/badwolfzm/pc/issues/61) identifies a lost guest-exit notification when the bounded personality queue is saturated; [PR #60](https://github.com/badwolfzm/pc/pull/60) alone is insufficient. Preserve the eight-slot cap and require a verified correction before treating boot stability as established.

## Workload ladder (each stage must pass before higher-stage compatibility claims)

| Gate | Workload | Must be demonstrated in Warden/QEMU | External requirements |
| --- | --- | --- | --- |
| G0 | Native, self-contained Doom-like raster loop | Draw deterministic moving framebuffer, measured checksums, time and memory; no guest escape | No game data; deterministic test fixture |
| G1 | Freestanding software-rendered interactive game | Keyboard events, timer pacing, framebuffer updates with one vCPU, bounded memory, exit/cleanup | User-provided input/event adapter |
| G2 | Open-source Doom-compatible engine, software renderer | Launch an actual named, pinned executable plus legally supplied game assets; menu/gameplay, input, frame correctness and clean exit | Build/port engine; SDL-like video/input/audio abstraction or direct adaptation; filesystem mapping |
| G3 | Unmodified Linux-native Doom source-port binary | Start an **unchanged** executable via Linux ABI, with ELF/linker/libc and its file/graphics/audio calls satisfied or fail precisely | Dynamic/static ELF as required; process, signals, files, clocks, memory mapping, device abstraction |
| G4 | Albion Online compatibility inventory | Collect owned installation's binary/dependency manifests and syscall/API requirements **without executing or bypassing protections**; report missing pieces and hardware constraints | Authorized copy and current vendor requirements; no binary redistribution |
| G5 | Albion Online launch attempt | Vendor-permitted, unmodified client starts, renders a frame, handles input; then assess login/network compatibility and stability | 3D graphics APIs/shaders, dynamic libraries, networking, process and threading APIs, audio, authentication and anti-cheat **without circumvention** |

**Important:** Albion Online is an online proprietary 3D title, not equivalent to Doom's software renderer. It may require an allowed OS environment, launcher, anti-cheat, accelerated graphics, and server-side acceptance. A single-core low-FPS target does not make those requirements disappear. Passing G2/G3 does not imply G5 is possible. Any EULA/anti-cheat prohibition is a hard stop, not an engineering challenge to bypass.

## Initial benchmark contract (targets, not measurements)

A reproducible run records executable SHA-256, source revision, toolchain/flags, input trace/asset license, QEMU version + acceleration (`tcg` or `kvm`), host CPU/RAM/OS, `-smp 1`, guest RAM budget, VM/process count, requested resolution and frame cap, and raw frame timestamps. Capture warmup and at least 300 timed frames when possible, including the 50th/95th/99th percentile frame time, achieved FPS, checksum mismatches, CPU time, peak guest memory **and** peak host resident set, syscall failure histogram and denied capabilities. Report boots and exits across at least two separate full QEMU runs, not merely a screenshot or best-case FPS.

Use two **example** constrained profiles for comparability (do not claim supported configurations):

* Small: 320×200, **64 MiB guest memory cap**, **one vCPU**, **10 FPS target**.
* Moderate: 640×400, **256 MiB guest memory cap**, **one vCPU**, **20 FPS target**.

If the existing Warden kernel cannot boot under those caps, record that as a blocker, not a game failure. Memory limits include guest kernel, personality server, assets and framebuffer; don't confuse per-thread untyped budgets with total VM RAM. A frame is successful only if output is checked against a deterministic expected result or reviewable reference capture.

Example test invocation once an actual guest runner exists (NOT a command currently offered by this repository):

```text
run-warden-game --vcpus 1 --guest-ram-mib 64 --width 320 --height 200 \
  --fps-limit 10 --input tests/fixtures/doom-input.trace --frames 300
```

Do not put this hypothetical invocation in CI until its actual implementation exists.

## Architectural contracts to implement, not shortcuts

1. **Video:** capability-granted framebuffer surface; bound dimensions, pixel format, stride and allocation; copy/flip with checked offsets, quotas and no access to other guests. Avoid host framebuffer access from untrusted pointers. MatrixOS CPU renderer is the early backend.
2. **Input:** a narrowly scoped keyboard/mouse event queue, bounded in bytes/events, with deterministic replay and no global keyboard access.
3. **Clock:** monotonically increasing frame deadlines and explicit virtual-time policy; denied/untrusted wall-clock or randomness must not be misrepresented as trusted.
4. **Assets and filesystem:** capability-rooted read-only asset grants first, checked symlink/path behavior and file-backed mappings as the Linux app requires; untrusted WADs and resource files are adversarial inputs.
5. **Linux ABI:** don't silently stub required `futex`, `clone`, `execve`, `mmap`, `poll`, signals, TLS or ELF dynamic loader semantics. Every unsupported API has an observable failure contract.
6. **Audio/network/3D:** optional for first headless Doom proof; G5 requires an independently reviewed protocol/API plan rather than declaring CPU rendering equivalent to a GPU.
7. **Exit and denial:** full-queue, malformed commands, out-of-bounds surfaces, exhausted memory, stale cap, cross-guest input/framebuffer and guest death must terminate or recover without leaking, hanging or broadening grants.

## Dependency and agent handoff

* **Kernel owner:** fix [#61](https://github.com/badwolfzm/pc/issues/61) and reconcile [PR #60](https://github.com/badwolfzm/pc/pull/60); rerun QEMU twice before G1 integration.
* **Graphics owner:** coordinate with [PR #58](https://github.com/badwolfzm/pc/pull/58); confirm freestanding SIMD/render state, not merely host-side C tests.
* **ELF/libc owner:** coordinate with [#29](https://github.com/badwolfzm/pc/issues/29) and [PR #59](https://github.com/badwolfzm/pc/pull/59). The PR validates metadata; it does **not** load dynamic programs.
* **Process owner:** [#28](https://github.com/badwolfzm/pc/issues/28); do not assume full Linux threading/process lifecycle exists.
* **This contribution:** documentation only; no kernel/ABI/MatrixOS source modifications and no ownership of other agents' paths. All performance and game-execution claims remain **not verified**.

## Ready-to-accept evidence

To mark **G0** complete, provide a link to the exact test source and executable, verified deterministic output, resource limits, twice-repeated boot/exit, and measured single-vCPU frame statistics. To mark **G2**, show a named Doom engine and game assets actually running under Warden, input and frame evidence, and clean teardown. To mark **G3**, prove the executable is unmodified. To mark **G5**, prove permitted launch plus actual rendered output before claiming playability; report whether login/networking was attempted and permitted.

**Status on 2026-11-05:** roadmap only. No Doom or Albion executable was benchmarked or launched by this contribution. Independent review and control/bookkeeping reconciliation required before merge.
