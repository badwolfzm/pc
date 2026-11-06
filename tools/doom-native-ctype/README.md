# Warden native Doom: fixed C-locale `__ctype_toupper_loc` provider

This contribution supplies **one real external symbol** remaining from the merged Doom freestanding engine object gate. It implements the x86-64 **glibc-header** C-locale uppercase lookup ABI with a fixed 384-entry `int32_t` table, shifted by 128 exactly as expected by callers compiled using the glibc `<ctype.h>` macros. Bytes 0..255 map only ASCII `a`–`z` to uppercase; EOF (-1) stays -1. Negative non-EOF `char` values are outside the C ctype function contract, but their in-table entries are bounded and do not cause out-of-bounds reads. There is no allocator, syscall, locale mutation, TLS setup or host I/O.

This is **not** a drop-in general glibc implementation. There is no support for `setlocale`, per-thread locale switching, multibyte Unicode uppercase, other architectures/ctype ABIs, or moving this provider into a general Linux personality. Doom's pinned freestanding compiler/import profile is the only claimed target. Future compiler/toolchain changes must revalidate that ABI rather than silently relying on this table.

Host check: `cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fno-builtin -fsanitize=undefined tools/doom-native-ctype/doom_ctype.c tools/doom-native-ctype/test_ctype.c -o /tmp/test-doom-ctype && /tmp/test-doom-ctype`.

Native object: `cc -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -c tools/doom-native-ctype/doom_ctype.c -o /tmp/doom-ctype.o`.

The dedicated GitHub workflow clones exact upstream `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`, builds the **genuine** engine through the already merged `tools/doom-native-link-gate`, links this provider using `ld -r`, and requires that **only** `__ctype_toupper_loc` disappears. The CI report leaves critical `system`/file imports unresolved deliberately. Python negative tests also reject duplicate or unexpected providers.

**Verification boundary:** the output is an ET_REL partial link, **not** a freestanding executable, not running Doom in Warden, not measured 512 MiB RAM or FPS, and not evidence that Albion Online works. This only removes one specific runtime blocker. Independent ABI/security review before merge.

Task claim: `doom-c-locale-toupper-import-20261106`, recorded on issue #95. Exclusive new paths `tools/doom-native-ctype/**` and its dedicated workflow. Does not modify any other agent's files.
