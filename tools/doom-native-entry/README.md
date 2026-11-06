# Native Doom entry point and bounded game frame call path

A narrowly scoped freestanding `_start` candidate for Warden's x86-64
ring-3 entry contract. The boot loader must supply a stack and map
every PT_LOAD segment; the existing example guest mapping is only
**2 MiB**, too small for an 8 MiB Doom arena. This PR neither
changes that mapping nor claims a boot.

The entry passes four mutable arguments to **genuine** upstream
`doomgeneric_Create`: `doomgeneric -iwad freedoom1.wad -nogui`.
The IWAD filename is only a configuration choice; it does **not**
install an asset grant or make `freedoom1.wad` accessible. The game may
exit before its first Tick if the actual Doom initializer detects
missing assets/services.

After creation, it invokes `doomgeneric_Tick` up to **300** times
and terminates through the already merged, Warden-specific
`exit(int)` adapter (`SYS_EXIT=6`, never `SYS_HOST_EXIT=7`).

Host CI tests the entry itself with mocked Doom Create/Tick and exit,
then compiles its actual x86-64 freestanding object with **real original
pinned Doomgeneric** (79 engine units + five Warden adapters) and
the already merged thread-exit provider. An object-level gate requires
`_start` defined, no unexpected imports, and all sensitive host
process/file symbols unresolved. This is the native startup ABI
**in a relocatable ET_REL only**, not a real guest executable or QEMU
test. The host mock has no WAD/gameplay and 300 ticks do not imply FPS.

This is a necessary entry-path contribution, not a complete production
crt0/glibc or general Linux startup. It does not provide argv from
the loader, ABI auxv, TLS, a file grant, error logging or heap
initialization beyond Doom's own calls. User-mode stack and 512 MiB
guest-wide residency remain unverified.

Issue #270, exclusive new `tools/doom-native-entry/**` and dedicated
workflow; independent kernel/loader/runtime-owner review before merge.
