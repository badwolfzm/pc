# Doom freestanding strtod feasibility probe — diagnostic, not a libc provider

Task: [issue #209](https://github.com/badwolfzm/pc/issues/209) (\`doom-freestanding-strtod-diagnostic-20261106\`).

The purpose is to check a specific remaining C runtime import in **real pinned upstream Doomgeneric** with Warden's existing freestanding adapter/heap build, while **showing rather than hiding numerical correctness problems**.

This independent C-locale prototype parses common decimal and hexadecimal numbers, inf/nan and signed zero without accessing host storage, spawning processes, or importing Linux syscalls. It is **not correctly rounded** in all cases. An initial local deterministic differential test on Debian/GCC 14.2 observed **34 wrong last-bit results out of 100,000 long decimal strings in the original approximate parser** vs the host libc. Common 394-case representative input tests passed. These are observations, not performance benchmarks or proof of broad conformance. The new bounded exact-comparison path handles at most 3,000 significant decimal digits and uses IEEE binary64 round-to-nearest/ties-to-even; on Debian GCC 14.2 it matched host libc on 1,000,000 deterministic long decimal inputs (0 bit mismatches), 15 midpoint/overflow/underflow fixtures, and 394 representative cases. Large inputs, hexadecimal rounding, alternate rounding modes, locales, errno details and ABI remain incomplete. **Not a production libc implementation.**

## Checks

Run from the repository root:

\`\`\`sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all -Dstrtod=warden_strtod -c tools/doom-native-strtod/strtod.c -o /tmp/strtod-probe.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all /tmp/strtod-probe.o tools/doom-native-strtod/test_strtod.c -lm -o /tmp/strtod-check
LC_ALL=C /tmp/strtod-check
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all /tmp/strtod-probe.o tools/doom-native-strtod/stress.c -lm -o /tmp/strtod-stress
LC_ALL=C /tmp/strtod-stress # now enforces zero mismatches on 1,000,000 cases
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined,address -fno-sanitize-recover=all /tmp/strtod-probe.o tools/doom-native-strtod/test_rounding.c -lm -o /tmp/strtod-rounding
LC_ALL=C /tmp/strtod-rounding
python3 -m unittest discover -s tools/doom-native-strtod -p test_verify.py -v
\`\`\`

The dedicated GitHub workflow checks the numerical diagnostic; compiles freestanding C without unexpected imports; fetches pinned original Doomgeneric \`e257a09527062bfb986b6e5e0780839b6cbd5b71\`; executes the already-merged true native Doom ET_REL linker gate; and requires **only \`strtod\`** to leave the unresolved symbol set, with all sensitive host file and process operations left unresolved. \`__errno_location\` intentionally remains unresolved: no unsafe global errno placeholder.

## Resource and precision constraints

The exact path uses 512 32-bit limbs per big integer. GCC 14.2 reports approximately 10,480 bytes stack in the exact helper and 112 bytes in `strtod` (host build at -O2); measured by compiler stack-usage analysis, **not Warden guest stack instrumentation**. The compiled freestanding object has approximately 5.6 KiB text/data on local x86-64 GCC 14.2. Runs under fixed stack/compute/digit budgets and is not yet reviewed for adversarial time complexity. When exact bounds fail, the prior approximate parser remains in use and is **not guaranteed correctly rounded**.

**Explicit nonclaims:** The ET_REL output is not an ET_EXEC, not a Warden guest, not a QEMU boot, not a working Doom game, not measured FPS, not 512 MiB guest/shared-GPU memory accounting, not Albion Online compatibility, and not a production-quality standards-conforming strtod. This diagnostic **must not be merged or integrated as the actual Warden runtime implementation**. Choose a correct, appropriately licensed libc implementation and independently review ABI/TLS/locale behavior before native game integration.

Owned only new \`tools/doom-native-strtod/**\` and \`.github/workflows/doom-native-strtod.yml\` for #209. No edits to kernel, other CRT, WAD, security, graphics, shared runtime or coordination files.

## Further bounded hexadecimal rounding proof (2026-11-06)

A new isolated `exact_hex.h` uses the same fixed-capacity exact-integer midpoint comparison as the decimal path for **hexadecimal** significands and binary `p` exponents. It handles nearest-even rounding, normal/subnormal/overflow boundaries, optional sign and signed zero, within the existing maximum of 3,000 significant digits and a fixed 512-word bigint budget. Explicit out-of-budget fallback **remains approximate**. No allocation, syscalls or new runtime authorities are introduced.

Local Debian x86-64 GCC 14.2 tests in this session: 100,000 deterministic hexadecimal strings (varying mantissa length, exponent and sign) versus glibc, **0 bitwise/endptr mismatches**; 28 adversarial boundary cases passed under AddressSanitizer/UBSan; freestanding `nm -P -g -u` still lists only `__errno_location`; native object text ~7,097 bytes, no data/BSS. These are host comparisons—not Warden/QEMU measurements, proof of exhaustive IEEE conformance or proof of 512 MiB gaming capability.

Run the added tests with the renamed host object, e.g.:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all -Dstrtod=warden_strtod -c tools/doom-native-strtod/strtod.c -o /tmp/strtod-o.o
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all /tmp/strtod-o.o tools/doom-native-strtod/test_hex.c -lm -o /tmp/strtod-test-hex
LC_ALL=C /tmp/strtod-test-hex
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all /tmp/strtod-o.o tools/doom-native-strtod/hexstress.c -lm -o /tmp/strtod-hexstress
LC_ALL=C /tmp/strtod-hexstress 100000
```

The math, ABI/locale/errno and time bounds still need independent review before any merge or production use. The explicit `ready_for_runtime_integration:false` result is intentionally unchanged.

## Range-error correctness regression (2026-11-06)

The original exact-rounding prototype set `errno=ERANGE` unconditionally for all nonzero subnormal binary64 outputs (incorrect for exactly representable values such as `0x1p-1074`) and sometimes omitted `ERANGE` for inexact decimal underflows such as `5e-324`. This change adds a bounded exact rational-versus-double comparison to recover **inexactness**, and compares against `DBL_MIN` when an inexact result rounds to the smallest normal double. It changes **only the experimental, owned `strtod` files**; freestanding symbol dependencies still include only existing `__errno_location`.

Executed locally (Debian x86-64, GCC 14.2, glibc 2.41): 21 selected C-locale `errno`/result/endptr fixtures passed, 394 representative strings passed, 15 decimal rounding boundaries passed, 28 hex boundary cases passed, 100,000 hex strings had zero binary-output/endptr mismatches. Compiled with `-fsanitize=undefined` and freestanding `-ffreestanding -fno-builtin`; `nm -P -g -u` still showed only `__errno_location`. The host freestanding object grew to approximately 8,089 bytes text and no data/BSS in this local GCC configuration—**not actual guest RAM usage**.

**Known limitations, do not erase:** There are at least two remaining glibc 2.41 `errno` differences for positive inputs near `DBL_MIN` that round upward: `0x1.fffffffffffff8p-1023` and `2.2250738585072012e-308` (details of the host library's underflow signaling depend on its internal conversion path). They are deliberately excluded from the 21-case narrow acceptance gate. Alternate IEEE rounding modes, long-input fallback, locale, hostile workloads and TLS behavior still require review. This PR is **not** a conforming libc and must stay draft until independent review.
