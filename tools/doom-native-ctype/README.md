# C-locale `__ctype_toupper_loc` provider for genuine Doom's native link

**Task:** `doom-freestanding-ctype-table-20261106` · [issue #200](https://github.com/badwolfzm/pc/issues/200) · independent proof for #95.

`ctype.c` is a self-contained, read-only, 384-int lookup table matching the x86-64 glibc-style `__ctype_toupper_loc` macro ABI for the **C locale**. Index range is -128..255, valid standard C inputs are EOF or 0..255. The legacy negative signed-char extension is supplied for testing but is not valid portable C input. It does not create system calls, an OS thread, a file-service authority, any allocator, or a Linux kernel.

Run host checks:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all tools/doom-native-ctype/ctype.c tools/doom-native-ctype/test_ctype.c -o /tmp/warden-doom-ctype
/tmp/warden-doom-ctype
python3 -m unittest discover -s tools/doom-native-ctype -p 'test_verify.py' -v
```

The dedicated workflow fetches **real pinned upstream Doomgeneric** (`e257a09527062bfb986b6e5e0780839b6cbd5b71`), runs the existing Warden native-link gate over its real 79 freestanding engine source files and merged platform/WAD adapters, partial-links this provider, and requires **exactly `__ctype_toupper_loc`** to vanish from undefined symbols. All protected host file/process calls must remain unresolved. It archives the full remaining undefined-symbol report.

**Not a guest executable:** this provides just one minor C-locale ABI entry point, not dynamic locales, TLS/per-thread locale support, a general glibc implementation, a runnable Doom ELF, valid Warden process lifetime, framebuffer/input integration, or a 512 MiB guest/full-machine proof. It must not be included in general Linux compatibility until the runtime owner signs off on ABI/locale behavior. No real native Doom frame or FPS is claimed. It intentionally makes *no* claims about Albion Online's much larger Linux ABI and 3D graphics requirements.

Paths are isolated to `tools/doom-native-ctype/**` and `.github/workflows/doom-native-ctype.yml`, with no changes to any other agent's runtime, kernel, WAD, graphics, existing CRT, tests, or control ledger. Review by an independent owner before any integration/merge.
