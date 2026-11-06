# Native Warden Doom: strictly bounded integer formatting

This independent implementation provides the two previously unresolved, freestanding C library imports `snprintf` and `vsnprintf` in the genuine pinned Doomgeneric engine. It supports the C-locale, no-float subset: `%d`, `%i`, `%u`, `%o`, `%x`, `%X`, `%s`, `%c`, and `%%`; decimal/`*` width, decimal/`*` precision, `-+ #0` flags, and integer size modifiers `hh`, `h`, `l`, `ll`, `j`, `z`, `t`. Width and precision overflow, unsupported formats (`%f`, `%e`, `%p`, `%n`, positional arguments), and NULL `%s` explicitly return -1, with a zero-length output buffer, rather than silently generating false results. It does **not** implement general libc, printf, fwrite, file descriptors, locale, any float, or a security-sensitive host process/file API.

`snprintf` never writes beyond `capacity` and always NUL-terminates if `capacity > 0` and the call succeeds. Returns would-be length excluding terminator; accepts a null buffer only for `capacity == 0`, and returns -1 if the would-be length exceeds `INT_MAX`. Invalid formats are rejected rather than adding new unexpected libc calls. It performs zero allocations and uses fixed local buffers only. The caller must still supply valid pointers and format specifiers; the capability boundary validates guest buffers separately.

Host checks (testing the provider under renamed symbols to compare with glibc):

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all -Dsnprintf=warden_snprintf -Dvsnprintf=warden_vsnprintf -c tools/doom-native-format/doom_format.c -o /tmp/warden-format-test.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fsanitize=undefined -fno-sanitize-recover=all tools/doom-native-format/test_format.c /tmp/warden-format-test.o -o /tmp/warden-format-test
/tmp/warden-format-test
python3 -m unittest discover -s tools/doom-native-format -p 'test_*.py' -v
```

CI compiles unrenamed x86-64 freestanding provider, pins genuine `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`, builds merged Warden engine/adapter `ET_REL` through `tools/doom-native-link-gate`, and checks **only** `snprintf`/`vsnprintf` disappear from unresolved imports. Protected file/process imports remain unresolved.

This is deliberately **not** an executable Warden game: no evidence of the real Doom engine booting in native Warden/QEMU, no 512 MiB resident-memory measurements or fps, and no Albion Online claim. Unimplemented float or pointer formatting could be used by the engine at runtime and must be resolved explicitly before a real guest release. Independent review and exact-head CI required before integration.

Task: `doom-native-bounded-format-20261106` claimed in issue #95. Scope limited to new `tools/doom-native-format/**` and `.github/workflows/doom-native-format.yml`.
