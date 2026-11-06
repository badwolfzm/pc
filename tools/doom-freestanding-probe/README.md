# Genuine Doom freestanding compilation diagnostic

Issue #128. This builds **real pinned upstream Doomgeneric engine translation units** with x86-64 freestanding compiler flags to identify source-level blockers *before* the full static link. It does not provide an OS ABI, complete libc, ELF loader, Doom runtime or WAD file. The original source is fetched from upstream at SHA `e257a09527062bfb986b6e5e0780839b6cbd5b71`, not copied into this repo.

```sh
python3 -m unittest discover -s tools/doom-freestanding-probe -p 'test_*.py' -v
git clone https://github.com/ozkl/doomgeneric.git /tmp/doomgeneric
git -C /tmp/doomgeneric checkout --detach e257a09527062bfb986b6e5e0780839b6cbd5b71
python3 tools/doom-freestanding-probe/probe.py --upstream /tmp/doomgeneric/doomgeneric --out /tmp/doom-native-objects
```

The JSON `report.json` includes every translation unit and actual compiler diagnostic. Diagnostic mode exits 0 even if some compile units failed, so CI can preserve those errors; **a green diagnostic workflow is NOT evidence that the actual freestanding engine compiled**. For a strictly enforced compiler gate, add `--strict-compile`. Compiler success still leaves undefined symbols and must be followed by a real freestanding static link with reviewed Warden syscall/asset/heap implementations.

The compiler flags specify freestanding, no compiler builtins, no stack protector, non-PIE, and `-mgeneral-regs-only` on x86-64. This is not a claim that a 2 MiB native Warden guest will fit, or that QEMU on one vCPU will run Doom. It does not infer anything about Albion Online. The published source and results require independent review and control-lead integration approval. Do not merge automatically.

## Observed compiler blocker and A/B gate

Initial GitHub Actions [run 42185051231](https://github.com/badwolfzm/pc/actions/runs/42185051231) produced **77/80** successful upstream compilation units with `-mgeneral-regs-only`. The three failing files were `g_game.c`, `m_config.c`, and `v_video.c`, each with GCC's `SSE register return with SSE disabled` diagnostic. This is an actual source/toolchain blocker for the current strictly general-register-only policy. The follow-up workflow compares it against an x86-64 System V register-policy build **without** that restriction; using SSE in a native guest requires independent review and kernel FPU/SIMD save/restore (PR #58), and is NOT justified merely by compiler success. The unresolved symbol report deducts definitions from successfully compiled units; when units fail, symbol closure remains incomplete and must not be treated as a full-engine link result.
