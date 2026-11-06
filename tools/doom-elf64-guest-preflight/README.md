# Doom final ELF64 guest-image preflight (host only)

This isolated tool is for **the next native Doom milestone** after a complete freestanding executable has been linked. It parses the actual final **ELF64 ET_EXEC** image, not the earlier `ET_REL` partial-link object. It rejects bad/truncated headers, non-x86-64 or dynamic/interpreter images, bad segment file ranges, address arithmetic overflow, noncanonical user ranges, virtual overlap, writable+executable load segments, executable stack requests, invalid segment alignment and an entry point outside executable code.

Usage after you have a *real final* x86-64 static Doom executable:

```sh
python3 tools/doom-elf64-guest-preflight/preflight.py path/to/doom.elf --guest-mib 512 --reserved-mib 128
python3 -m unittest discover -s tools/doom-elf64-guest-preflight -p 'test_*.py' -v
```

`--reserved-mib` is an **explicit assumed budget** for the Warden kernel, other guest allocations, mapped framebuffer/shared integrated GPU allocation and other overhead. It is not a measurement; choose it from real evidence when available. The 512 MiB goal represents **total guest physical memory**, not a per-game allowance or a second 512 MiB of GPU memory. In a real integrated-graphics system GPU allocations can share physical RAM; externally reserved GPU or firmware allocations must be accounted for separately if outside QEMU guest RAM. One vCPU (`-smp 1`) is a separate QEMU/guest acceptance parameter and cannot be proven by inspecting ELF bytes.

Page-rounded loaded image footprint is only a conservative *static load-page estimate*, not exact kernel frame accounting or runtime peak RSS. It excludes page tables, stack, heap, WAD data, caches, kernel, graphics, runtime libraries, frame changes and GPU/host overhead. Because the native loader may have different restrictions, an accepted image is **not necessarily loadable**. A final boot requires native Warden ring-3 integration, two genuine single-vCPU QEMU runs, memory accounting and rendered gameplay/input evidence.

The CLI returns exit 0 for accepted preflight and exit 1 for rejected/missing files, with machine-readable JSON. No subprocess, WAD, network, kernel, process execution or Linux personality/syscall access. It is **not a security verifier**, authorization decision, or a substitute for independently reviewed loader security. It must not be wired into the privileged runtime without kernel-owner review.

Task #192; parent Doom milestone #95. Paths owned: `tools/doom-elf64-guest-preflight/**`; no existing kernel/CRT/graphics code changed. Review and exact-head CI required before merging. Albion Online additionally requires legitimate Linux libraries, loader, GPU, audio, networking and title compatibility and may not fit this budget.
