# Bounded freestanding allocator prototype for real Doom integration

A **compile-time configurable fixed static arena** (default 4 MiB, validated 1 MiB profile) offering `doom_alloc`, `doom_free`,
`doom_calloc`, `doom_realloc` and live-byte accounting. Uses a
first-fit split/coalesce linked-block design. Exhaustion returns NULL.
Heap metadata is stored in the arena, and no host heap is used in the allocator.

This is **not integrated into the Doom engine**, and **not a Warden guest**.
An existing Warden guest layout reserves only about 2 MiB address space:
the default 4 MiB build CANNOT be inserted unchanged. The 1 MiB profile may be a better starting point but has **not** been linked with the genuine engine or validated in Warden. Its capacity is deliberately
a prototype limit, **not a measured guest minimum**. The kernel/loader owner
must review layout expansion or an appropriately bounded smaller arena
after real engine high-water measurements, without weakening isolation.

## Verification

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  tools/doom-freestanding-arena/arena.c \
  tools/doom-freestanding-arena/test_arena.c -o /tmp/doom-arena-test
/tmp/doom-arena-test
cc -std=c11 -ffreestanding -fno-builtin -Wall -Wextra -Werror \
  -c tools/doom-freestanding-arena/arena.c -o /tmp/doom-arena.o
```

Tests cover aligned allocations, calloc overflow and zeroing, realloc
preserving bytes, allocation failure without destroying prior allocation,
fragment reuse and coalescing. Symbols are prefixed `doom_` to avoid
accidentally replacing libc. `memcpy`/`memset` will still require a
reviewed freestanding implementation when linked into Warden.

Limitations: trusted **single-thread** input only; not safe under concurrency,
not hardened against arbitrary bad pointers, not a complete libc replacement.
`doom_arena_reset` invalidates all pointers. `doom_arena_used` tracks
requested live bytes, not metadata, reserved bytes, or physical RAM.
To select a smaller build use `-DDOOM_ARENA_BYTES=1048576` for **1 MiB**, as exercised by CI. The configured capacity must be between 512 KiB and 16 MiB and is not dynamic swap. A linked executable must test actual allocation patterns and WAD sizes,
then report OOM, peak and two single-vCPU QEMU runs. Independent review
needed before production use.

Coordination: issue #112; new unique paths only. Does not edit PR #88,
#109, #110 or shared kernel/loader files.
