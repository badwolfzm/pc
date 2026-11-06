# Shared-GPU 512 MiB budget checker

Task: [#191](https://github.com/badwolfzm/pc/issues/191). Independent bounded helper, not Warden kernel instrumentation. Supports the merged [#134](https://github.com/badwolfzm/pc/pull/134) vision. Does not modify or supersede the existing draft [#146](https://github.com/badwolfzm/pc/pull/146) report acceptance gate.

## Accounting contract

This **shared-memory GPU profile** has one guest vCPU, a maximum **512 MiB total resident guest physical memory**, **zero dedicated GPU VRAM**, and an up-to-512-MiB shared-GPU allocation ceiling. The latter is **not** additional memory: shared resident GPU pages must already be included in the `graphics_mib` bucket. Buckets `kernel_mib`, `game_mib`, `personality_mib`, `graphics_mib`, `paging_mib`, `other_mib` are disjoint resident *physical* memory and must include metadata, caches, framebuffers, buffers, and reserved pages counted against guest RAM. Virtual address space, virtual GPU aperture, swap backing store and host RSS are different quantities, not summands. Do not convert a large virtual aperture to claimed resident memory.

Inputs are self-reported JSON in MiB. Example:

```json
{
  "kernel_mib": 32,
  "game_mib": 300,
  "personality_mib": 20,
  "graphics_mib": 96,
  "paging_mib": 32,
  "other_mib": 16,
  "gpu_shared_mib": 80,
  "gpu_shared_limit_mib": 512,
  "gpu_dedicated_mib": 0,
  "vcpus": 1
}
```

Run `python3 tools/shared-gpu-memory-budget/validate.py report.json`; run tests using `cd tools/shared-gpu-memory-budget && python3 -m unittest discover -p 'test_*.py' -v`.

## Limitations and owner handoff

This is a simple, **self-reported accounting** gate, not independently gathered telemetry. Passing JSON cannot prove that a VM booted, that graphics allocation is correctly instrumented, that the physical guest cap was enforced, or that Doom or Albion runs. A *single host process* is a separate goal from one *guest vCPU*; this checks only vCPU count. Native adoption must meter actual resident physical pages, prove GPU and CPU mappings alias rather than double-allocate, and account for host-side GPU driver memory without pretending it is inside guest RAM. If GPU shared pages are held outside a separately measured guest pool, the instrumentation must report that physical usage separately and reject an unaccounted budget.

No source, kernel, ABI, security boundary or permission changes. Independent reviewer and game/graphics owner should integrate true telemetry into draft #146 or its successor; avoid merging overlapping acceptance definitions without coordination. No FPS measurements performed.
