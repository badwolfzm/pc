# Freestanding Doom memory/string primitives

Bounded, single-thread reference functions required by real Doom's native static linker work (#94 / #95). Does not include upstream engine code, WAD files, heap allocation or a loader.

```sh
make -C tools/doom-libc-primitives test freestanding
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin -fsanitize=undefined -Itools/doom-libc-primitives tools/doom-libc-primitives/doom_libc.c tools/doom-libc-primitives/test_primitives.c -o /tmp/doom-libc-ubsan && /tmp/doom-libc-ubsan
```

The default API is prefixed `wd_`, avoiding a collision with the host libc. The freestanding compile opts in to C standard symbol exports with `-DWARDEN_DOOM_EXPORT_LIBC_NAMES`; do not link those names against the host libc in tests. These functions take valid pointers and lengths, including the standard non-overlap requirement for memcpy. There is no automatic pointer-capability validation. `memmove` assumes flat x86-64 integer-address ordering and handles overlapping ranges.

`strlen` is unbounded by definition; `strnlen` offers a bounded alternative. Real engine data may still require more functions, heap integration, a complete static guest link and kernel security review. Do not claim gameplay, resource budget, FPS or Albion compatibility based on this prototype.

Claim #116; owned paths `tools/doom-libc-primitives/**` and a dedicated workflow. Keep draft until exact-head CI and independent review.
