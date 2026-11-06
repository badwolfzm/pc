# Freestanding Doom bounded formatting provider

Task #210. Strict C11 x86-64 freestanding `snprintf` and `vsnprintf` **subset**, no heap, no OS access. This is a native C runtime ingredient, not a real game boot.

Supported conversions: `%s`, `%c`, `%d`, `%i`, `%u`, `%x`, `%X`, `%o` and `%%`; integer length modifiers `hh`, `h`, `l`, `ll`, `z`; numeric width, precision, left/zero padding, sign, and hex `#` prefix. Strings support precision, and truncate to the output buffer. Bounds: width/precision ≤ 1,000,000, output character count ≤ INT_MAX; returns `-1` for unsupported directives, malformed format strings, or overflow rather than forging a successful formatted output. Do not feed untrusted or attacker-mutated format strings to this implementation: it is not an isolation boundary, and invalid pointers remain caller errors.

**Not yet implemented:** `%f`, `%e`, `%g`, `%a`, `%p`, `%n`, dynamic `*` widths/precision, positional parameters, locale, wide characters, alternate octal `%#o`, plus/space for unsigned integer formats and complete ISO C printf conformance. The real Doom callsite inventory must be reviewed before this becomes a production runtime provider; unsupported format returns -1 and may break Doom until expanded. **Do not** use this as a drop-in full libc replacement, or assume it is safe for all printf callers.

The independent dedicated CI checks runtime C tests under UBSan; confirms the object has no unresolved external imports; clones and pins original `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`; runs the already merged real source compiler/linker gate; performs a real `ld -r` partial link; and demands exactly the two targets disappear from genuine engine imports, without resolving protected host file/process imports.

Run the unit suite with:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined -fno-sanitize-recover=all tools/doom-native-format/format.c tools/doom-native-format/test_format.c -o /tmp/test-doom-format
/tmp/test-doom-format
```

Owned new paths: `tools/doom-native-format/**` and `.github/workflows/doom-native-format.yml`. No Warden kernel, loader, game, host filesystem, WAD or other agent paths modified. The authentic upstream engine code is not copied here.

**Not verified:** final ET_EXEC, native Warden ring-3 boot, Doom WAD gameplay/input, a 512 MiB total guest shared-graphics budget, one vCPU, or FPS. Independent integration/security/code review is needed. Do not merge based only on linker import closure.
