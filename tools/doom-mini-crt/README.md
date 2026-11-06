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


## C-locale string expansion (follow-up to native Doom link gap)

This directory additionally provides `strcasecmp`, `strncasecmp`,
`strncpy`, and `strstr` in `mini_extra.c`. These are four real missing
symbols in the merged freestanding whole-engine partial-link diagnostic #170.
They do not require a heap, libc, syscalls or host OS. `strcasecmp` and
`strncasecmp` implement **ASCII/C-locale only**, not arbitrary locale-specific
case conversion; no locale state is supported. `strncpy` has standard
non-overlap, valid-input preconditions, pads with zero bytes, and does not
guarantee termination when the source length is at least `n`. `strstr`
returns the first matching substring or the original haystack for an
empty needle. Test sources are host-side and intentionally depend on libc for
`assert` and `puts`, but both compiled library objects must have zero
undefined symbols. Existing CI now runs both strict and UBSan test suites.

**Do not mistake these four additional string definitions for resolution of
Doom's remaining allocator, fortified formatting, file-system or process
service imports.** Rebuild the exact combined freestanding object and compare
`nm -u` on its new revision before counting eliminated linker gaps. This
does not install the source into a native guest automatically. The guest owner
must select the reviewed object and independently verify real QEMU execution.
