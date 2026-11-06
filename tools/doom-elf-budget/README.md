# Doom native guest ELF budget preflight (D1)

This **host-side, Python standard-library-only** checker validates a candidate
fixed-address x86-64 ELF64 executable's PT_LOAD mapping within the currently
observed Warden G0 0x800000–0xA00000 (2 MiB) guest window. It rejects dynamic
ELFs, non-executable entry points, malformed headers, oversize images, page
collisions, and W+X segment mappings. It accounts for page-rounded static loaded
bytes and conservative additional stack/staging reserves.

```sh
python3 tools/doom-elf-budget/check.py path/to/candidate.elf
python3 -m unittest discover -s tools/doom-elf-budget -p 'test_*.py' -v
```

The default extra reserve is 128 KiB stack and 307,200 bytes for the PR #88
Doomgeneric 320x240 staging framebuffer. **These reserves are conservative**:
if the staging buffer already appears in ELF .bss it will be counted twice.
Use --staging-bytes 0 only when the staging memory is demonstrably covered
by the ELF's mapped segments; otherwise retain the conservative default.
Actual engine heap, WAD, kernel frames/page tables, mmap, fragmentation and
runtime resident peak are NOT measured. Real native Warden loader acceptance,
capability grants, in-guest behavior and Linux dynamic ELF compatibility are
separate gates. A green result does not imply playable Doom or Albion or FPS.

Ownership: tools/doom-elf-budget/**, isolated from native G0 (#80),
Doomgeneric callback adapter (#88), WAD preflight (#74/#77), kernel (#60/#73)
and host resource envelope (#96). Issue #94 records D1.
Do not merge until independently reviewed.
