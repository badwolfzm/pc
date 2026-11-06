# Native Warden Doom: bounded puts adapter

Claim [#251](https://github.com/badwolfzm/pc/issues/251). Standalone
freestanding replacement for one of the unresolved genuine engine imports:
`puts`. Its only output is a writer explicitly installed by trusted guest
bootstrap, using `warden_doom_set_console_writer`. If no writer is installed,
if the string is more than 1023 bytes, if the writer fails or short-writes, or
if input is null, it returns -1 without host stdout access.

All writes are chunks of at most 128 bytes, followed by exactly one newline;
success is 0, failure -1. No heap, syscall, host libc, fd, network, ambient
file, configuration or game asset access. A callback is **not** itself an
authorization mechanism: the kernel/guest console service owner must supply a
grant-checked writer. Callback installation/revocation is not thread-safe;
do it before guest execution. The input must point to readable guest memory;
a future syscall boundary independently validates pointers.

Limitations: Line length cap is an intentionally stricter subset of C puts,
so long strings return -1. Writer failure may leave partially emitted text.
Not a generic `printf`, `fprintf`, `stdout`/FILE* provider, not a native
Doom boot, and not a 512 MiB RAM/FPS/Albion measurement.

Test locally:
```sh
cc -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -fno-builtin -Dputs=warden_guest_puts -c tools/doom-guest-puts/puts.c -o /tmp/warden-puts.o
cc -std=c11 -Wall -Wextra -Werror -O2 tools/doom-guest-puts/test_puts.c /tmp/warden-puts.o -o /tmp/test-warden-puts
/tmp/test-warden-puts
```

CI additionally compiles actual pinned upstream Doomgeneric plus existing
Warden adapters, performs `ld -r` with this provider, and requires exactly
`puts` to be removed from the unresolved symbol set. This is NOT a runnable
ELF or proof of runtime compatibility. Independent review before merge.
