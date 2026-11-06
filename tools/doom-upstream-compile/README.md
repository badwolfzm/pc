# Genuine upstream Doom engine build inventory

Tracks real-engine gate #94. **No Doom source, commercial WAD, or third-party binaries are vendored.**

Pins the open-source [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) revision `e257a09527062bfb986b6e5e0780839b6cbd5b71`. At that exact revision, reads the engine's `doomgeneric/Makefile.soso` `SRC_DOOM` object manifest, omits only the OS-specific `doomgeneric_soso.o` backend, compiles all other genuine upstream C translation units with a host C compiler to relocatable objects and inventories the remaining external symbols using `nm`. Output is JSON.

```sh
python3 -m unittest discover -s tools/doom-upstream-compile -p 'test_*.py' -v
git clone https://github.com/ozkl/doomgeneric.git /tmp/doomgeneric
git -C /tmp/doomgeneric checkout --detach e257a09527062bfb986b6e5e0780839b6e5e0780839b6cbd5b71
python3 tools/doom-upstream-compile/inventory.py --source /tmp/doomgeneric/doomgeneric --output /tmp/doom-inventory
```

The revision verification must run at the **repository root**, not its subdirectory (git -C resolves upwards). No unpinned upstream updates. The CI workflow performs the clone and writes an artifact.

**What this proves when green:** the real upstream engine's translation units compile with a *host* compiler (not mock C renderer). It identifies missing functions needed for a future portable libc/allocator/WAD/clock/input layer. **What this does not prove:** successful link, freestanding/no-libc ABI, available guest heap, integrated engine running inside Warden, licensed WAD execution, playable Doom, RAM or FPS. The independent Warden kernel and PR #88 platform callback adapter remain separately owned. Engine remains GPL-licensed: comply with the upstream license for any combined distribution.

## Second gate: host link with the real Warden adapter

The CI additionally fetches **exact** Warden PR #88 adapter revision
`c60e37f85a1f704290234ec95a1aae60622fbb51` (refusing unexpected movement),
compiles its original `DG_*` implementation against the pinned genuine
Doomgeneric header, and links it with all 80 upstream engine objects and the
small `link_entry.c` main entry. This deliberately uses the Linux **host
libc and libm** to discover remaining source-level symbol and cross-module
compatibility problems before attempting a freestanding native guest port.

**Never run** `doom-host-link-only` on the host: its adapter intentionally
performs Warden `int 0x80` syscalls not defined for Linux. Green CI proves a
host link, not a working Warden guest or an acceptable native runtime.
The workflow saves `host-link-layout.txt`, `host-link-size.txt` and
`host-link-imports.txt` for review. In particular, *all* remaining libc,
libm and loader imports must be handled explicitly before a native freestanding
link; no claim is made that the host ELF fits Warden's current narrow guest
memory map. Review warnings and inspect the actual image layout as a separate
gate. The adapter and the kernel remain owned by their current agents.
