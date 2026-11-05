# Dirty-bit canvas compositing (ghost canvas)

Owner: thread "Design dirty-bit canvas compositing". Status: **reference
implementation, tested, benchmarked on a dev container, not yet wired to the
RCS command set or the asset streamer.** Aligned with
`contracts/gpu-commands.md` (wave 1 release, 2026-11-05 07:08) and
`assets/asset_store.h`. The security review has not landed yet; re-read against
it when it does.

## What it does

The render service keeps two copies of every presentable surface:

| Buffer | Holds | Written by |
|---|---|---|
| `work` | what the game has drawn so far this frame | CLEAR, FILL_RECT, BLIT, LINE, DRAW_TRIANGLES |
| `ghost` | the last frame the encoder/streamer received | `gc_present()` only |

Plus one **dirty bit per 16x16 tile** (not per pixel) and one summary bit per
tile row. A draw sets the tile bit only when a pixel's value actually changes.
`gc_present()` then:

1. walks the row-summary word, skipping clean tile rows with one `ctz`;
2. for each dirty tile, XOR-compares `work` against `ghost` row by row and
   stops at the first difference;
3. if nothing differs (A->B->A inside one frame, or a `gc_damage` over
   unchanged pixels) the tile is dropped: `tiles_false` counts it;
4. otherwise copies the tile into `ghost`, calls the sink (the encoder's input)
   with the tile's rectangle, and counts it;
5. clears the bit. After present, `ghost == work` and every bit is zero.

`gc_invalidate_all()` marks every tile and sets `force`, so the next present
sends the whole frame without the XOR test (new client, key-frame request).

### Why this replaces the per-pixel XOR toggle in the uploaded MatrixOS file

`mx_canvas_mark` in `matrixos_v9.c` XORs one shadow bit per pixel on every
change and never resets it. Three problems, all demonstrated by tests here:

- **Lost update.** A pixel written A->B->C in one frame toggles twice and reads
  clean. `test_old_toggle_loses_abc` reproduces it on the old scheme and
  shows the tile scheme emits the pixel.
- **No frame boundary.** The shadow is never cleared at present, and
  `changed_count` only grows, so there is nothing to walk per frame.
- **Full-frame clear.** `mx_canvas_clear` rewrites every pixel each frame and
  marks ~100% dirty. `gc_clear` is tile-wise and marks only tiles whose pixels
  change, so a clear to the same colour costs a compare and emits nothing.

A 16-pixel tile matches the macroblock size of H.264 and the block size most
software encoders use, so the dirty-tile list is directly the encoder's
"blocks to re-encode" list; no second damage pass is needed in the encode stage.

## Cost per frame

Fixed sizes (independent of hardware):

| | 480x270 | 1280x720 |
|---|---|---|
| Pixels | 129,600 | 921,600 |
| Frame bytes (ARGB8888) | 518,400 (0.49 MB) | 3,686,400 (3.5 MB) |
| Tiles (16x16, ceil) | 30 x 17 = 510 | 80 x 45 = 3,600 |
| Dirty bitmap + row summary | 72 B | 464 B |
| Memory for work + ghost | 1.0 MB | 7.0 MB |
| Per-pixel 1-bit shadow (old scheme) would be | 16,200 B, scanned fully | 115,200 B, scanned fully |

Measured on the dev container (Intel Xeon 2.10 GHz, 4 vCPU, one thread used,
gcc -O2 with the matrixos profile flags, no SIMD tier flag, median of 9 runs of
60 frames; `make bench` reproduces, raw numbers in `bench.csv`). "present" is
the whole composite: XOR compare, copy into ghost, memcpy of each tile into a
client buffer standing in for the encoder input. Encoding itself is not
included.

| Scene | Size | Draw | Present | Tiles sent | Bytes sent per frame |
|---|---|---|---|---|---|
| Static (nothing drawn) | 480x270 | 0 | <1 us | 0 | 0 |
| One 64x64 sprite moving | 480x270 | 11 us | 4 us | 27 | 27.6 KB (5.3%) |
| 20 sprites + HUD bar (~10% of screen) | 480x270 | 39 us | 32 us | 240 | 245 KB (47%) |
| Full redraw | 480x270 | 89 us | 73 us | 510 | 518 KB (100%) |
| Static | 1280x720 | 0 | <1 us | 0 | 0 |
| One 64x64 sprite moving | 1280x720 | 11 us | 4 us | 27 | 27.6 KB (0.7%) |
| 20 sprites + HUD bar | 1280x720 | 93 us | 85 us | 422 | 433 KB (11.7%) |
| Full redraw | 1280x720 | 697 us | 683 us | 3,600 | 3.69 MB (100%) |

Reading: the worst case, a full 720p redraw, composites in about 0.7 ms, which
is 2% of the 33 ms budget at 30 fps and matches the "about 0.5 ms" line for the
background composite in the design doc. A typical 2D frame (sprites over a
static background) composites in well under 0.1 ms and hands the encoder 1 to
12% of the frame. The spread of scattered 48x48 sprites at 480x270 touches
47% of tiles even though it writes about 10% of pixels; tile count, not pixel
count, is what the encoder sees, so sprite placement granularity matters.

Compiler check: gcc 13 and clang produce byte-identical client buffers on every scene (the bench prints an FNV-1a hash of the whole streamed buffer, so the output is consumed and nothing is dead code). clang is faster on the full-redraw draw stage (0.47 ms against 0.69 ms at 720p: it vectorises the tile-wise clear better); present times are within noise. Run-to-run noise on the shared container is up to 1.7x on the sub-100 us rows, so compare rows, not single numbers.

Tile size was checked at 8, 16 and 32. 32 cuts the bitmap but is slower on a
full 720p redraw (0.93 ms, worse cache behaviour on the 128-byte tile rows) and
sends 40% more bytes for a single sprite. 8 sends the fewest bytes but 4x the
tiles and is slower on every scene with real work. 16 stays.

Full redraw moves 3.7 MB of work into ghost and 3.7 MB on to the sink, about
11 GB/s effective here, which is the single-core memory-bandwidth ceiling the
design doc assumes (10 to 20 GB/s). Numbers from the user's target hardware
are the memory-bandwidth thread's job and will move these by a constant factor.

## Tests

`make test OUT=/tmp/build` (the project-files mount is noexec; 759 checks, 0 failures), `make asan` (AddressSanitizer + UBSan) and `make ubsan` (UBSan, no recovery), all 0 failures:

| Test | Checks |
|---|---|
| First present sends every tile once, edge tiles clipped, pixels sum to w*h | 480x270 gives 30x17 tiles and exactly 129,600 pixels |
| Second present with nothing drawn | 0 tiles, 0 bytes |
| A->B->C lost update | old toggle reads clean; new scheme emits 1 tile, ghost holds C |
| A->B->A | tile flagged, XOR drops it, 0 emitted |
| Clear to same colour | 0 tiles dirty; a 10x10 rect inside one tile emits exactly 1 tile, 256 px |
| Bounds | fully outside draws mark nothing; clipped draws land on the right pixels and tiles |
| `gc_damage` on unchanged pixels | all flagged, none emitted; direct write + damage is emitted |
| Sink abort | nonzero sink return propagates |
| Fuzz, 300 frames 480x270, 60 frames 720p, 200 frames 37x19 | after every present: ghost == work, streamed client == work, all bits clear, emitted + false == dirty |
| Determinism | same scripted 30 frames hash to the same ghost twice: `f7da60a90fb9597f`; bench client-buffer hashes identical under gcc and clang |

Not tested: alpha/additive blends and triangles (not in this module yet, see
below), concurrent access (single-threaded by design), A8 surfaces.

### Dirty-tile edge tests (`test_dirty_tiles.c`, `make test_dirty`)

62 checks in a second file that pins the bitmap mechanics the table above only
covers end to end. They also run under ASan and UBSan (`make asan`).

| Test | What it pins |
|---|---|
| Bitmap word boundary (1280x720) | tiles 62..66 of row 0 straddle bit 64; tile 79 of row 1 is the last bit of a row; every dirty word is consumed |
| Row summary, 64x1100 (69 tile rows) | rows 0, 63, 64, 68 are walked; the second summary word is used; the 12 px partial row is clipped to 16x12 |
| Tile edges | pixels at 15 and 16 dirty different tiles; a damage rect ending on a tile border does not spill; zero-size rects mark nothing |
| `gc_pset` clipping | out-of-range writes mark nothing; same-colour writes are free |
| `gc_invalidate_all` | forces a full 480x270 resend (510 tiles, 0 false), then clears `force` |
| `gc_damage_bits` | NULL and mismatched geometry return -1 and touch nothing; a flagged tile with no changed pixel is dropped (`tiles_false`); the partial corner tile of 37x19 is 5x3 |
| Sink abort | the first nonzero return is propagated, the sink is not called again, every bit is still cleared, the frame counter advances, the ghost still matches work |
| Validation and sizing | sizes 0 and above 16384 are rejected; `gc_bitmap_bytes` gives 72 and 464 as in the cost table; a 1x1 canvas works; `gc_free` nulls the pointers |

Mutation check (run once by hand, not in CI): dropping the second row-summary
word, shifting the word-span mask by one bit, and skipping `force` in
`gc_invalidate_all` each make this file fail.

## Alpha blending rounding rule

Owned by the command-set contract, `contracts/gpu-commands.md` section 4
(normative, shared with canvas and superops): per colour channel with `a` the
source alpha, `t = s*a + d*(255-a) + 128; out = (t + (t >> 8)) >> 8` (exact
round(t/255)); destination alpha unchanged; ADD saturating on all four bytes;
COPY replaces. `test_blend_rules` in `rcs_test.c` is the oracle; the canvas has
no blend code of its own.

## How it plugs into the RCS contract

`gc_canvas` is the backing store of an `rcs_surface` that is a PRESENT target.
Mapping of draw opcodes:

| RCS op | Canvas call | Dirty marking |
|---|---|---|
| CLEAR | `gc_clear` | tile-wise compare |
| FILL_RECT | `gc_rect` | per pixel compare |
| LINE | `gc_line` | per pixel compare |
| BLIT, BLEND_COPY | `gc_blit` | per row memcmp |
| BLIT, BLEND_ALPHA / ADD | super-op kernel writes `work` directly, then `gc_damage(dst rect)` | rect, XOR at present drops unchanged tiles |
| DRAW_TRIANGLES | rasterizer writes `work`, then `gc_damage(triangle bbox)` | same |
| PRESENT | `gc_present(sink = encoder)` | clears bits |
| SET_CLIP | clip the rect before any call above | none |

The `gc_damage` path is what lets the SIMD super-op thread own the blend
kernels: they do not need to know about dirty bits, only to report the
rectangle they wrote. The XOR compare at present catches kernels that wrote
the same value. `RCS_FRAME_PIXEL_BUDGET` is charged by the RCS layer before
the call, not here.

### Two ways the compositor can receive a frame

1. **Canvas is the surface's backing store** (preferred, what this module
   assumes). The service executes CLEAR/FILL_RECT/LINE/BLIT(COPY) through the
   `gc_*` calls, and blend/triangle kernels write `work` then call `gc_damage`.
   Present cost scales with what changed.
2. **Service hands an opaque ARGB surface at PRESENT.** Then the compositor has
   no damage information and must XOR-diff the whole surface against the ghost:
   `gc_damage(all)` + `gc_present` = the "full redraw" row above, measured
   0.65 ms at 720p, 0.07 ms at 480x270, every frame even when static. Still
   within budget, but it wastes the dirty bits. The command-set contract now
   hands the compositor `rcs_present_info` at PRESENT with a per-surface 16x16
   tile bitmap (`tiles`, `tw` x `th` bits, row-major, bit = ty*tw + tx, LSB
   first), the same geometry as the canvas bitmap. `gc_damage_bits` ORs it in
   (test: 26 service tiles imported, 2 emitted after the XOR check, 24 dropped
   as unchanged), which turns mode 2 into mode 1 with no rect list and a
   bounded record (450 bytes at 720p).

## Asset store glue (`canvas_assets.[ch]`)

Built against `matrixos/assets/asset_store.h`. `gc_blit_image` blits a sub-rect
of a mapped ARGB8888 image with explicit stride and rejects out-of-image
rects; `gc_blit_asset` wraps `asset_query` + `asset_map` and returns
`ASSET_NOT_READY` without drawing when the mip is not resident (never blocks);
`gc_restore_tiled_bg` restores the background under a moved sprite from an
`assetpack --tiles` layout (16x16 tiles, row-major, 1 KiB each) by whole-tile
copies; `gc_assets_poll` drains `asset_poll` once per frame outside
BEGIN_FRAME..PRESENT and damages the canvas when the bound background's
generation changes or it is evicted. `make test_assets` (22 checks: sub-rect
blit, stride > width, four geometry rejections draw nothing, tiled restore
returns the exact background and emits exactly the 25 tiles under a 64x64
sprite at the bottom edge, binding invalidation rules). `make glue` compiles
the store-backed wrappers against the real header. Not run against a store on
disk yet.

## Baseline patch

`baseline-patches/0001-canvas-bench-consumed-hash.patch` fixes the uploaded
file's canvas benchmark, whose `consumed_hash` was always 0 (identical
iterations XOR-cancelled). Verified by building the baseline and the patched
copy: CSV `consumed_hash` goes from 0 to a nonzero value, the 30-frame demo
PPM SHA-256 `99e7937b...4414` is unchanged, and
`test_consumed_hash_does_not_cancel` reproduces the cancellation and checks the
fix. The uploaded file itself is not modified (CONTROL.md decision 1).

## What this module needs from asset streaming

Agreed with the "Design GPU asset streaming from storage" thread on 2026-11-05 (all four accepted, no conflicts). Their draft API: `asset_open`, `asset_want`, `asset_drop`, `asset_query` (non-blocking, resident-mips bitmap + generation; a blit on a non-resident asset gets `ASSET_NOT_READY`), `asset_poll` (list of (asset, mip, generation) changed this frame; the canvas calls it outside BEGIN_FRAME..PRESENT and turns each entry into `gc_damage`). Header to land in `matrixos/assets/asset_store.h`. Resident buffers are never evicted or replaced inside the frame window; tiled 16x16 backgrounds are a pack-layout option.

1. A bound IMAGE asset is readable as ARGB8888, 4-byte aligned, with an
   explicit stride in pixels, for the whole of BEGIN_FRAME..PRESENT. The canvas
   copies on blit and keeps no pointer across a present.
2. Blit of an asset that is not resident must fail synchronously (an RCS error
   or a documented "not ready" status), never block the render thread on
   storage. The canvas has no stall path.
3. A per-asset generation counter that changes when the streamer replaces the
   bytes of a resident asset, so a cached background restore can be
   invalidated with `gc_damage`.
4. Pre-baked backgrounds should be served as the same 16x16 tile grid (tile
   row-major, 1 KB per tile) so that restoring the background under a moved
   sprite is a tile copy, not a strided rect copy. Optional, saves about 30%
   on the sprite scenes above; not measured yet.

## Audit fixes (independent audit, 2026-11-05)

The audit found four UBSan-confirmed signed overflows on extreme coordinates
(`gc_damage` x=INT_MAX, `gc_rect` x=INT_MAX, `gc_line` INT_MIN..INT_MAX,
`gc_blit` x=INT_MIN) and noted the line rasterizer could run ~4 billion steps.
Fixed as follows:

- All primitives clip in 64-bit arithmetic **before** any loop (`gc_clip_rect`),
  so iteration counts are bounded by the canvas, not by the arguments.
  `gc_restore_tiled_bg` in the asset glue got the same treatment.
- `gc_line` no longer rounds clipped endpoints. Bresenham is a linear
  recurrence, so its state after k iterations has a closed form
  (`n = (2kb + a) / 2a`, `err = a - b - kb + na`, verified against brute force);
  the rasterizer jumps to the first visible major-axis step and runs at most
  canvas-extent iterations. Pixels are identical to the unclipped line (test:
  200 random lines with far-off endpoints, exact match against a brute-force
  reference), and the golden hash is unchanged.
- Regression test `test_extreme_coordinates`: the four probes, INT-range
  lines in every orientation, 2,000 random INT-range lines in under 500 ms,
  a 64x1 blit declared as INT_MAX x 1 that reads only 64 pixels. `make ubsan`
  runs the suites with `-fsanitize=undefined -fno-sanitize-recover=all`; the
  audit's four probe programs now exit 0 in about 3 ms each on the fixed code.
- Uses `__int128` for two intermediates (gcc and clang; MSVC would need a
  fallback).

## Uncertainties and what is not verified

- Timings are from a shared cloud container, not the target. Treat as the
  ratio between scenes and as an upper bound on compositor share of the frame,
  not as absolute numbers.
- The encoder cost (6 to 10 ms in the design doc) dominates and is outside this
  module. Whether the dirty-tile list reduces it as expected depends on the
  encoder accepting a block mask; not verified.
- Security review not yet read. Known points to check against it: all pixel
  indexes are bounded by `w`,`h` set at init; `gc_blit` trusts `stride` and
  `w`,`h` of the source (the RCS layer validates them against the asset before
  calling, per `RCS_E_RECT_OUTSIDE`); no allocation after init; no paths or
  pointers cross the client boundary.
- Single surface per canvas. Off-screen surfaces that are never presented do
  not need a ghost and should not pay for one; that split is not implemented.
- A8 format surfaces are not handled.
