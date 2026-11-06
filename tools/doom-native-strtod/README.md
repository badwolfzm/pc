# Doom freestanding strtod feasibility probe — diagnostic, not a libc provider

Task: [issue #209](https://github.com/badwolfzm/pc/issues/209) (\`doom-freestanding-strtod-diagnostic-20261106\`).

The purpose is to check a specific remaining C runtime import in **real pinned upstream Doomgeneric** with Warden's existing freestanding adapter/heap build, while **showing rather than hiding numerical correctness problems**.

This independent C-locale prototype parses common decimal and hexadecimal numbers, inf/nan and signed zero without accessing host storage, spawning processes, or importing Linux syscalls. It is **not correctly rounded** in all cases. An initial local deterministic differential test on Debian/GCC 14.2 observed **34 wrong last-bit results out of 100,000 long decimal strings** vs the host libc. Common 389-case representative input tests passed. These are observations, not performance benchmarks or proof of broad conformance. Decimal to binary conversion drops trailing digits, so it is *unsuitable for general production libc or game runtime integration*.

## Checks

Run from the repository root:

\`\`\`sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all -Dstrtod=warden_strtod -c tools/doom-native-strtod/strtod.c -o /tmp/strtod-probe.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all /tmp/strtod-probe.o tools/doom-native-strtod/test_strtod.c -lm -o /tmp/strtod-check
LC_ALL=C /tmp/strtod-check
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all /tmp/strtod-probe.o tools/doom-native-strtod/stress.c -lm -o /tmp/strtod-stress
LC_ALL=C /tmp/strtod-stress
python3 -m unittest discover -s tools/doom-native-strtod -p test_verify.py -v
\`\`\`

The dedicated GitHub workflow checks the numerical diagnostic; compiles freestanding C without unexpected imports; fetches pinned original Doomgeneric \`e257a09527062bfb986b6e5e0780839b6cbd5b71\`; executes the already-merged true native Doom ET_REL linker gate; and requires **only \`strtod\`** to leave the unresolved symbol set, with all sensitive host file and process operations left unresolved. \`__errno_location\` intentionally remains unresolved: no unsafe global errno placeholder.

**Explicit nonclaims:** The ET_REL output is not an ET_EXEC, not a Warden guest, not a QEMU boot, not a working Doom game, not measured FPS, not 512 MiB guest/shared-GPU memory accounting, not Albion Online compatibility, and not a production-quality standards-conforming strtod. This diagnostic **must not be merged or integrated as the actual Warden runtime implementation**. Choose a correct, appropriately licensed libc implementation and independently review ABI/TLS/locale behavior before native game integration.

Owned only new \`tools/doom-native-strtod/**\` and \`.github/workflows/doom-native-strtod.yml\` for #209. No edits to kernel, other CRT, WAD, security, graphics, shared runtime or coordination files.
