# Real Doom / Warden freestanding link-closure gate

The **actual next engineering blocker** after successful Linux-host 300-frame gameplay is producing a freestanding, kernel-loadable native ring-3 Doom ELF. This is a source-backed compiler/linker experiment using `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71` with its real Makefile source manifest and unaltered upstream C files. It excludes upstream OS backend `doomgeneric_soso.o` and upstream host stdio WAD backend `w_file_stdc.o`, then builds **79** real engine translation units + five actual Warden source components with `-ffreestanding -fno-builtin -fno-stack-protector -fno-pie`.

It then links those objects as **one relocatable native-ABI object** using `ld -r`, enumerates remaining truly unresolved external symbols using `nm -g -u`, and writes `gate.json` with imports categorized into host I/O, allocation, math and other. Unlike prior name-only inventory (#127), this derives the unresolved set from the combined genuine engine + adapters + mini-CRT build, so symbols already supplied by Warden disappear from the blocker list.

Run:

```sh
python3 -m unittest discover -s tools/doom-native-link-gate -p 'test_*.py' -v
python3 tools/doom-native-link-gate/gate.py --source /pinned/doomgeneric/doomgeneric --output target/doom-native-link-gate
```

GitHub CI clones and verifies the immutable upstream commit. No upstream source/WAD is distributed in this repo; the engine uses GPL licensing. No DRM bypass or external game/process data access occurs. Only authentic source and Warden's merged adapters are compiled; no Linux kernel substitute.

**Limits and interpretation:** `ld -r` succeeds with unresolved references, so it is **NOT** a complete link, bootable guest, or proof of 512 MiB game compatibility. The report deliberately marks `native_executable_built:false` and `native_warden_boot_verified:false`. Remaining host libc imports require explicit secure Warden-native service/allocator choices, including correct process/window/heap backing and authorized files; weak stubs must not paper over unresolved security-sensitive operations. Real Doom must still pass two native QEMU single-vCPU boots, with a Warden guest RAM cap and actual live memory accounting. Do not call this FPS.

Owned only `tools/doom-native-link-gate/**` and dedicated CI workflow, in coordination with #95. No kernel, upstream engine, existing linker, CRT, WAD, guest mapping, or other owner's files edited. Review ABI assumptions and PIC/no-PIC and target x86-64 compilation against the real Warden guest loader before integration.
