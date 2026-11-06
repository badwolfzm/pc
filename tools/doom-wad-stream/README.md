# Doom WAD read-only stream (G1 integration prerequisite)

Independent, freestanding-compilable **C reference** for streaming WAD bytes through a storage owner's explicitly authorized `read_at` callback. It is **not** a storage driver, kernel service, WAD preflight replacement, or Doom engine.

The parser checks the 12-byte IWAD/PWAD header, directory count/bounds, and 16-byte lump metadata on access. No heap allocation or direct filesystem/device access. Callback returns 1 only for the entire requested read; partial data returns 0. A WAD need not reside fully in RAM. Limits: **64 MiB source size; 65,536 directory entries**. Failed reads do not advance cursor. Caller must authenticate and authorize source, enforce callback length contract, and provide integrity protection. Entries are validated individually, not exhaustively on open.

Run `make test`, `make sanitize`, and `make freestanding`. Freestanding object may still require minimal memcpy/memcmp/memset at final link.

## Integration
1. Storage owner provides capability-scoped read_at and trusted source size, without raw-device probing or unrestricted path lookup.
2. Engine owner binds Doom's WAD I/O functions to these APIs or reviewed equivalents.
3. Native Warden/QEMU must boot the actual engine with one vCPU and report genuine frames/peak guest memory before declaring Doom support.
4. Negative integration tests: unauthorized reads, truncated callbacks, unmapped guest pointers, exhaustion.

Task #95; owned paths `tools/doom-wad-stream/**` only. No changes to WAD preflight PR #74, precompute PR #87, Doomgeneric callbacks PR #88 or kernel owner files. Requires independent review before merge.
