# Doom D1 ELF virtual-address budget preflight

Issue #98 contributes a stand-alone, stdlib-only ELF64 fixed-address x86-64 preflight for #89. It **does not boot Doom**. It validates PT_LOAD bounds/file extents/alignment, executable entry, mapped-page overlap, and conservative aggregate address-space budget. It does not edit Warden kernel, graphics, ABI, storage, or other agent paths.

Usage (the address is an EXAMPLE, NOT verified Warden guest origin):

```sh
python3 -m unittest discover -s tools/doom-elf-budget -p 'test_*.py' -v
python3 tools/doom-elf-budget/check.py path/to/guest.elf --window-start 0x400000 --window-bytes 2097152 --stack-bytes 131072 --heap-bytes 262144 --asset-bytes 0
```

The kernel owner must supply the real guest linker origin and window before using this for acceptance. 2 MiB is a *planning limit*, not proof the Doom engine can fit. `--asset-bytes` is an extra reservation only for assets held **outside** ELF's mapped segments. Doomgeneric's 307,200-byte framebuffer staging buffer is already in ELF BSS when linked; do not double count it. Increments are rounded to 4 KiB.

Limitations: remaining aggregate bytes does **not** prove usable placement for stack/heap/assets, physical RAM consumption, runtime heap requirements, loader safety, W^X, paging, WAD loading, real QEMU single-vCPU performance, or gameplay. Unsupported: dynamic ELF shared libraries / Albion Linux binaries. No proprietary game data included.

Ownership: new `tools/doom-elf-budget/**` files only. Local prototype tests reported 10/10 passing before repository publication; exact-head CI and independent review required. This is a draft, not merge-ready game functionality.
