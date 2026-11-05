# matrixos/assets — asset streaming from storage

Owner: thread "Design GPU asset streaming from storage".
Design: [GPU Asset Streaming from Storage](https://claude.ai/artifact/2dFkx9rUz81isX258bpXim).
Baseline C file is not edited; this folder is a new module.

## What is here

| File | What it is |
|---|---|
| `asset_store.h` / `asset_store.c` | Read-only, content-addressed store reader with a non-blocking residency cache. One reader thread (HDD plan) or several (SSD plan). |
| `assetpack.c` | Packer: file or raw ARGB8888 image → 64 KiB chunks named by SHA-256 under `chunks/xx/`, plus a manifest under `manifests/<sha256>`. Images get a box-filtered mip chain; `--tiles` writes mip 0 as 16x16 ARGB tiles, row-major, 1 KiB each, for the canvas thread. |
| `test_asset_store.c` | 47 checks: jail escapes, hash verification, non-blocking API, residency, eviction, budget, generation counter, close-in-flight, SSD plan. |
| `bench_asset_store.c` | Read-pattern harness: sequential 4 MiB assets vs random 64 KiB chunks at queue depth 1, 4 and 32, plus the SHA-256 CPU ceiling. |
| `Makefile` | `make test`, `make asan`, `make bench`. |

## Interface for the canvas thread

All calls are non-blocking. A frame draws with what `asset_query` reports and never waits on the disk.

```c
asset_store_open(root, &cfg, &store)         // once; opens the directory fd the jail is relative to
asset_open(store, manifest_sha256, &h)       // load time: reads + verifies the manifest
asset_want(store, h, mip, priority)          // queue a mip for residency (0 = visible now … 255 = speculative)
asset_drop(store, h, mip)                    // no longer wanted; evicted at the next poll
asset_query(store, h, &status)               // resident_mask, wanted_mask, generation, geometry
asset_map(store, h, mip, &ptr, &len)         // ASSET_OK with a stable pointer, or ASSET_NOT_READY
asset_poll(store, events, max)               // once per frame, outside BEGIN_FRAME..PRESENT: applies completions and evictions, reports changes
```

Guarantees the canvas asked for and this module keeps: a mapped buffer is stable between two polls (test "mapping stays valid after drop until poll"); a blit on a non-resident mip gets `ASSET_NOT_READY` synchronously; `generation` changes on every replace or evict (tests "generation advanced", "bumps generation"); tiled backgrounds via `assetpack --tiles` (test "16x16 tiled, row-major, 1 KiB per tile"). IMAGE assets are ARGB8888 with `stride_px` in the status; the packer only emits 4-byte-aligned buffers and `malloc` returns 16-byte alignment.

## Security properties and how they are enforced

Addresses finding 1 (symlink escape) and the over-long path finding from `matrixos/reviews/security-review.md`.

- Every open is `openat2(dirfd, path, RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV)` relative to the store's `O_PATH` directory fd. No path string is validated and then `fopen`ed. Paths are generated from hex hashes, never from guest input, but the jail does not rely on that.
- The opened object must be a regular file (`fstat` + `S_ISREG`), so a directory or device under a chunk name is refused.
- Paths longer than 128 bytes are refused with `ENAMETOOLONG` before the syscall. Nothing is truncated.
- Every chunk is read in full and its SHA-256 compared with the manifest's before it becomes resident; a file longer than the manifest says is a mismatch too. The manifest's own bytes are hashed and must equal its file name.
- A hard link inside the store to a file outside it is not something `openat2` can refuse: a hard link is the same inode, not a path crossing. It is covered by the content check instead: the linked file's bytes do not match the chunk hash in the manifest, so the read fails with `ASSET_ERR_HASH` and nothing becomes resident. An attacker who can create hard links in the store can therefore cause a load failure, never a read of foreign bytes into a guest. (Noted by the tests thread; no test, since it needs the same filesystem and the outcome is the ordinary hash failure path already tested.)
- Residency is bounded by `hot_set_bytes`. A mip that can never fit is refused at `asset_want`; one that does not fit now evicts only mips with a worse priority, and fails with `ASSET_ERR_BUDGET` otherwise, so a greedy caller cannot push out a more important one.

## Verified (run here, on this container: Linux 6.18, 4 cores, ext4 on a virtual disk)

- `make test`: 47 passed, 0 failed, under `gcc -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- `make asan`: same 47 pass under AddressSanitizer + UndefinedBehaviorSanitizer with gcc, no reports. (clang ASan cannot link in this container; the tests thread confirmed the plain build under clang 18.)
- The tests thread's independent harness, `matrixos/tests/security_regressions.sh` with `ASSETS_DIR=<writable copy of this folder>`, passes 9 of 9 against this module (symlink out of root, `../`, absolute path, climbing directory symlink, 129/550/600-byte paths).
- Jail checks that pass: symlink file → `../` (ELOOP), path through a symlinked directory (ELOOP), absolute symlink (ELOOP), `../` escape, absolute path (EINVAL), directory as chunk, symlinked manifest through `asset_open` (`ASSET_ERR_ESCAPE`), 6000-byte path (ENAMETOOLONG) also when a file with the truncated name exists.
- Corruption check: one byte flipped in a chunk of the blob → `ASSET_EV_FAILED` with `ASSET_ERR_HASH`, nothing resident, `asset_map` answers `ASSET_NOT_READY`.
- `make bench` (32 × 4 MiB assets + 128 × 64 KiB chunks, page cache warm because the harness just wrote them; this is NOT the user's drive):

| pattern | MB/s end to end | notes |
|---|---|---|
| HDD plan, sequential 4 MiB assets, 1 reader | 225 to 230 | `pread` alone ran at 5 to 6 GB/s from cache; the gap is SHA-256 on one core |
| SSD plan, same assets, 4 readers, drained one asset at a time | 209 to 213 | serialized by the harness, so one hasher at a time |
| random 64 KiB chunks, queue depth 1 | 168 to 227 | |
| random 64 KiB chunks, 4 readers | 697 to 809 | 4 hashers in parallel |
| random 64 KiB chunks, 32 readers | 600 to 700 | no gain past the core count |
| SHA-256 in memory, one core, portable C | 255 | the ceiling everything above hits |

**Finding from the bench:** the portable SHA-256 in `asset_store.c` runs at about 255 MB/s per core, not the 1.5 to 2 GB/s the design assumed for SHA extensions. On any SSD the verifier, not the disk, is the bottleneck until hashing uses SHA-NI (or BLAKE3) or is spread across cores. On a 5,400 RPM drive (about 100 MB/s) it does not matter. The design doc's open question on the hash is now backed by a number.

## Not verified

- No run on the user's machine. The target is a Windows PC running the stack under WSL2; all numbers above come from this cloud container's virtual disk with a warm page cache.
- No cold-cache numbers: dropping caches needs root. `bench_asset_store` prints two passes and labels them.
- No run on a real 5,400 RPM drive or a real SSD. The HDD "plan" is only a scheduling order here; the extent layout (4 MiB extents in streaming order) that the design relies on for HDD throughput is not implemented by `assetpack`.
- LZ4 compression is in the design and not in this code; packs are raw.
- The host-side streamer / virtio-fs export and the per-VM budget from the Warden untyped capability are design only. This module is the guest-side reader plus cache, runnable on the host for now.
- Thread-safety was exercised by the tests with 1 and 4 readers under ASan, not by a race detector (no TSan run).

## Assumptions about WSL2

- The store must live on the ext4 root of the WSL2 distro (for example under `~/warden/store`), not under `/mnt/c`. `/mnt/c` is a 9p/drvfs mount: `openat2` resolve flags and `O_PATH` semantics are not guaranteed there, symlink behaviour differs, and throughput is a fraction of the native path. The tests create the scratch store relative to the current directory, so run them from the distro's own filesystem.
- WSL2's kernel is 5.15 or newer, which has `openat2` (needed: 5.6+). If a custom older kernel is in use the store open fails with `ENOSYS` at the first read.
- "Disk" means the distro's VHDX on NTFS, served through Hyper-V. The host page cache in the design is the WSL2 kernel's page cache; Windows caches the VHDX file separately, so cold numbers are two layers deep and the HDD figures in the design doc are a floor for the physical disk, not a prediction for WSL2.
- SHA extensions: whether the CPU has SHA-NI changes the verify ceiling by about 6x. Not detected or used by this code yet.

## Next

1. Replace the portable SHA-256 with a SHA-NI path (runtime detected) or BLAKE3, then rerun `make bench`.
2. Extent layout in `assetpack` (write chunks of one manifest contiguously into a pack file; the reader opens the pack and `pread`s by offset) so the HDD plan reads 4 MiB sequentially instead of 64 files.
3. LZ4 chunks with the decompress step in the guest's 4 ms headroom.
4. Run `make test && make bench` on the WSL2 target and replace the numbers above.
