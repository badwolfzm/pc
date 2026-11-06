# Native Doom: one real missing C-locale `__ctype_toupper_loc` provider

This narrowly scoped freestanding C11 object implements the x86-64 glibc
uppercase lookup ABI needed by the genuine pinned Doomgeneric freestanding
engine object. It has a read-only 384-element (1,536-byte) character mapping
for indexes `-128..255` and a pointer to index zero. ASCII `a..z` become
`A..Z`, all other byte values map to themselves, and EOF (`-1`) remains -1.
The lookup does not allocate, access files, request syscalls or accept host
locale data. It is **C-locale only**, not full glibc internationalization.

Tests rename the provider to avoid accidentally comparing a function with
itself via glibc; every byte is compared to independently computed ASCII
semantics and system `toupper()` under `LC_CTYPE=C`. Python tests use
`ld -r` on synthetic caller objects to prove exactly-one-symbol closure
and reject extra imports and false successes. The dedicated CI then uses
`tools/doom-native-link-gate/gate.py` to build the **actual** pinned upstream
`ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`
freestanding engine plus the merged Warden adapters; it performs an actual
partial `ld -r` with this provider and refuses any change beyond resolving
`__ctype_toupper_loc`.

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -D__ctype_toupper_loc=warden___ctype_toupper_loc -c tools/doom-native-ctype/ctype.c -o /tmp/ctype-test.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fsanitize=undefined -fno-sanitize-recover=all tools/doom-native-ctype/test_ctype.c /tmp/ctype-test.o -o /tmp/test-ctype
/tmp/test-ctype
python3 -m unittest discover -s tools/doom-native-ctype -p 'test_*.py' -v
```

**Not proven:** no full libc implementation, 512 MiB guest budget, Warden
`ET_EXEC`/native boot, gameplay, FPS, Linux dynamic loader or Albion support.
This is a real **freestanding ET_REL import** reduction only. Keep any PR
draft pending real pinned-source CI, independent review, and lead integration.
No overlapping edits to existing kernel, CRT, loader, graphics, WAD or engine
components; owns only `tools/doom-native-ctype/**` and dedicated workflow.
