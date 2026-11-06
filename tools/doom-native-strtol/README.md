# Native Warden Doom: freestanding `strtol` (one exact runtime symbol)

This independent implementation closes just the real missing `strtol` import from merged native Doom/Warden linker gate #170, without supplying arbitrary host file or process capabilities. It implements C11 base 0/2..36, ASCII whitespace/sign, hex-prefix handling, overflow saturation, `errno=ERANGE`, endptr position and no-conversion behavior. For invalid base it chooses `errno=EINVAL` and no conversion.

It uses `<errno.h>`: the host-style **`__errno_location` remains unresolved** and must be replaced by a reviewed Warden guest-local/per-thread error store before final ELF. No fake errno success stub, file, process, host system calls, or heap is provided.

CI runs a C test of conversion/overflow against Linux `strtoll` on x86-64, builds an x86-64 freestanding object and a genuine pinned upstream Doom + Warden partial link using merged `tools/doom-native-link-gate`, verifying that ONLY `strtol` disappears and no new import appears. The old `__errno_location` stays unresolved. This is ET_REL and not a native game boot or measured memory result. Independent review required.
