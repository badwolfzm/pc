# Doom ELF static load-footprint preflight

Independent host-only ELF64 x86-64 program-header inspector for a **candidate linked real-Doom executable**. A proposed guest window in Doomgeneric PR #88 is approximately 2 MiB. This tool fails closed when a candidate binary's PT_LOAD virtual-address span exceeds a specified cap; it does not measure actual RAM use or run an executable.

```sh
python3 -m unittest discover -s tools/doom-elf-footprint -p 'test_*.py' -v
python3 tools/doom-elf-footprint/elf_budget.py --limit-mib 2 path/to/candidate-doom.elf
```

Outputs JSON and exits 0 on static-layout acceptance; exits 1 on rejection or malformed ELF. Handles ELF64 ET_EXEC and ET_DYN but **does not** perform relocation. Restricts file size to 128 MiB and program headers to 512; validates entry point, offsets, sizes, alignment, non-overlap and overflow. This is intentionally stricter than a complete ELF loader and may reject otherwise valid binaries.

**Warning:** PT_LOAD layout is not a real Doom memory budget. Excludes stack, heap, WAD, framebuffer (the separate Doomgeneric adapter uses a ~300 KiB surface), dynamic libraries, page tables and kernel consumption. Passing does not indicate the executable will load or run. Real Doom requires a pinned legal source-port build, properly sourced WAD, capability-bounded file I/O, library support and two actual single-vCPU QEMU gameplay runs. Albion's proprietary Linux client has further compatibility and authorization constraints; never bypass anti-cheat.

Task #97; owned paths only `tools/doom-elf-footprint/**`. No shared kernel, Linux loader, swap, graphics or PR #88 changes. Independent review required.
