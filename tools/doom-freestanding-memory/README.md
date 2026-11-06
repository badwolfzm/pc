# Freestanding memory primitives for native Doom integration

This is an original, independent pure-C implementation of four **prefixed**
memory helpers: `doom_memcpy`, `doom_memmove`, `doom_memset`, and
`doom_memcmp`. The code uses byte loops and requires no runtime
allocation, syscalls, or host libc.

Build and verify from the repository root:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin \
  -fsanitize=undefined -fno-sanitize-recover=all \
  tools/doom-freestanding-memory/memory.c \
  tools/doom-freestanding-memory/test_memory.c -o /tmp/doom-memory-test
/tmp/doom-memory-test
cc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
  -c tools/doom-freestanding-memory/memory.c -o /tmp/doom-memory.o
nm -u /tmp/doom-memory.o
```

The final command should print **no undefined symbols**. The test compares
our functions with host libc using 1000 deterministic trials including
overlapping moves, zero length, varied offsets, full bounds, comparison,
set and copy. Use `-fno-builtin` in freestanding builds.

These helpers have no pointer validation: the caller must supply valid
memory ranges; memcpy ranges must not overlap. Not constant-time, not
optimized and not independently security-reviewed. They do not supply
file access, Doom engine linking or a guest binary. Names are deliberately
prefixed so they do **not** accidentally override libc globally; the
Doom integration owner must explicitly select, wrap and link them with
PR #113 allocator and the WAD/engine work after memory layout approval.

**No actual Doom, Warden/QEMU, one-vCPU FPS or Albion Online run** claimed.
Task #117; independent review and lead approval required.
