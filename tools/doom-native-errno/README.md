# Doom single-thread errno + genuine `strtol` dependency closure

Provides a very small freestanding `__errno_location` returning a stable pointer
to one writable `int` in each separately linked native game image. It has
**no TLS**, supports **one executing game thread per image only**, and cannot
replace glibc's dynamic-linker/thread-aware implementation for general Linux
apps. Game/process isolation is an untested kernel-loader responsibility.

The CI workflow compiles genuine pinned Doomgeneric at
`e257a09527062bfb986b6e5e0780839b6cbd5b71` with merged Warden adapters,
and links this provider plus merged `tools/doom-native-strtol/strtol.c`. It
requires both original unresolved imports `__errno_location` and `strtol`
to disappear, **exactly two** and no more; dangerous host calls remain
unresolved. It tests an independent synthetic linking oracle with negative
cases, pointer stability and errno constants under UBSan.

This is freestanding **ET_REL** only, not a bootable game executable, not
authenticated WAD/file access, not a 512 MiB guest measurement and not FPS
or Albion support. Do not add to general Linux personality or dynamic runtime
without replacing the static cell with reviewed per-thread TLS. Do not merge
without real-source CI + independent review.

Task #206; exclusive `tools/doom-native-errno/**` and its CI workflow.
