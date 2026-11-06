# Genuine Doom freestanding heap ABI integration

This isolated test **actually partially links** pinned genuine upstream Doomgeneric freestanding engine code and Warden display/WAD components from merged PR #170 with the existing bounded heap prototype of draft PR #113. A small adapter maps **only four standard C allocation symbols** (`malloc`, `calloc`, `free`, `realloc`) to the existing `doom_`-prefixed arena functions. Do not make a host-libc replacement through permissive stubs. The original allocator implementation remains owned by #113; its exact source SHA is staged temporarily into CI without merging another agent's branch.

The test compiles and links actual objects with `-ffreestanding -fno-builtin`, then uses `nm -g -u` to verify the baseline genuinely requires four allocations, the integrated binary has **none** of those imports, and the integration has added no new unresolved symbols. It archives the remaining imports and SHA-256 of the combined object.

## Validation

```sh
python3 -m unittest discover -s tools/doom-native-heap-closure -p 'test_*.py' -v
python3 tools/doom-native-heap-closure/close.py --source /path/to/pinned/doomgeneric --output target/native-heap-closure --arena-bytes 1048576
```

The arena's **1 MiB profile is a candidate only**, not observed peak RAM and not a guarantee that the game fits. The reserved heap occupies its full static BSS, with memory also required for engine statics, framebuffer (at least 320x240x4 bytes staging), stack, kernel, WAD and other services. This is not a runnable Warden guest, not a full link and not evidence of 512 MiB guest-wide residency or modern-PC game support. Heap allocation failure behavior for actual Doom is not yet tested; arena is single-thread only, requires independent review, and does not defend against all invalid pointers.

The next work is reviewed no-libc implementations for remaining formatting/file operations, privilege-safe WAD and configuration lifecycle, and a real guest loader/address map with total physical frame accounting. Do not confuse `ld -r` with final ELF linking or QEMU boot.

Exclusive owned paths: `tools/doom-native-heap-closure/**` and its dedicated workflow; dependency draft #113 exact commit `315cc925ebf67f67d883bfa4d507ebcbb6b84c10`. No code from other owners changed.
