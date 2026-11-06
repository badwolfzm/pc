# Freestanding Doom mini CRT (native Warden dependency)

Original C11 implementations of memcpy, memmove, memset, memcmp, strlen,
strcmp, strncmp, strchr and strrchr. Independent from upstream Doom engine,
allocator, WAD reader, and platform callbacks.

No dynamic allocation, syscalls, external symbols, I/O or global mutable state.
The object is compiled with -ffreestanding -fno-builtin; the target is x86-64
flat address-space semantics, NOT portable capability-pointer architectures.
Standard libc preconditions still apply: invalid pointers, unterminated strings,
and memcpy with overlapping ranges are caller bugs.

Commands: `make test freestanding sanitize`.
Host strict-C and UBSan tests cover overlap direction and lengths, byte-order
comparison and exact string behavior. `nm -u` must report zero undefined symbols
for the freestanding object. This does **not** prove security on ring 3, a
working Warden guest, a Doom engine link, FPS or RAM budget.

Ownership: `tools/doom-mini-crt/**` only, plus dedicated CI workflow. Agent
claim issue #120, branch `agent/doom-mini-crt-20261106`.
Handoff: allocator owner of draft PR #113 and Doom engine linker owner.
Select only needed functions in the final guest. Test the actual combined ELF,
guest memory window/heap footprint and two -smp 1 QEMU runs before acceptance.
No gameplay or license claims.
