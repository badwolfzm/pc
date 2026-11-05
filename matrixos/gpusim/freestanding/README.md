# Freestanding render core (graphics plan M0)

The render service has to run on Warden as its own ring-3 image, and Warden
has no libc. This folder holds everything the C render core (`rcs.c` +
super-ops) needs to build without a C library:

| File | What it is |
|---|---|
| `include/string.h`, `stdlib.h`, `assert.h` | The only headers the core sees (`-nostdinc`; `stddef.h`, `stdint.h` and `stdbool.h` come from the compiler) |
| `mxlibc.h`, `mxlibc.c` | memcpy/memset (`rep movsb`/`rep stosb`), memmove, memcmp, and a first-fit arena allocator (malloc, calloc, free) |
| `host_env.c` | Host stand-in for the Warden environment: hands mxlibc one arena and maps `mx_abort` to `abort` |
| `test_mxlibc.c` | Allocator and string tests: random alloc/free with patterns, alignment, full coalescing, overflow, invalid and double free abort |

The super-ops pick their tier with CPUID + XGETBV under `-DMXS_FREESTANDING`
instead of `__builtin_cpu_supports` (which needs libgcc's CPU init). AVX2 is
used only when the CPU has it and the kernel enabled the YMM state in XCR0.

## Run

```
make B=/tmp/gpusim fs-check
```

This does three things:

1. It builds `render_fs.o` (rcs.c + super-ops, freestanding) and `mxlibc_fs.o`, then lists their
   undefined symbols. It fails if anything other than `memcpy memmove memset memcmp mx_malloc
   mx_calloc mx_free mx_abort` is needed.
2. It runs `test_mxlibc` (ASan + UBSan).
3. It links the unchanged contract suite `rcs_test.c` against exactly `render_fs.o` plus the
   mxlibc allocator and runs it on the scalar, v128 and v256 tiers.

## Verified (cloud container, gcc 13.3, 2026-11-05)

- Undefined symbols: `memcpy memset mx_abort mx_calloc mx_free`, so there is no libc dependency.
- `test_mxlibc`: 19 passed, 0 failed.
- `rcs_test` against the freestanding object: 57 685 passed, 0 failed on each of the three
  tiers. The v256 run went through the new CPUID/XGETBV detection.

## Not verified yet

- The object has not run inside Warden. That is milestone M3, which needs shared-memory mapping
  and a larger user window.
- On the host, `rcs_test_fs` uses libc's memcpy. The mxlibc string routines are covered by
  `test_mxlibc` only.
- The allocator is O(blocks) per call. That is fine for tens of surfaces, but it is not a
  general-purpose malloc.
