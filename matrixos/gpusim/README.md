# matrixos/gpusim — simulated memory-based GPU (prototype)

A working "GPU" that lives in CPU RAM. It is the render service from
`../contracts/gpu-commands.md` running on one CPU core:

- **Commands:** all 14 RCS commands, through the reference validator in
  `../contracts/gpu-commands-ref/rcs.c` (envelope checks, rights, slots, geometry,
  budgets, two-pass all-or-nothing). Nothing here bypasses validation.
- **Execution:** `rcs.c` built with `-DRCS_USE_SUPEROPS` runs FILL_RECT, CLEAR,
  BLIT (COPY / ALPHA / ADD) and DRAW_TRIANGLES on the `../superops` kernels, so the
  scalar, 128-bit (SSE2, NEON on ARM) and AVX2 tiers all apply. LINE, UPLOAD and
  BIND stay on the reference code (LINE has no SIMD shape; the other two are memcpy).
- **Memory:** surfaces are plain CPU RAM (`calloc`), capped by the contract's 8 Mpx
  per client. PRESENT hands the back buffer and its 16×16 dirty-tile bitmap to a
  scanout step that copies only dirty tiles into a front buffer ("display memory").
- **Storage:** granted assets come from the on-disk asset store (`../assets`):
  `openat2` jail from the store's directory fd and SHA-256 on the manifest and on
  every 64 KiB chunk, unchanged. `gpusim_grant()` is the policy side (opens a
  manifest by hash, queues mip 0); `gpusim_poll()` between frames applies finished
  reads and refreshes the asset table. The frame path never reads the disk.
- **Ring:** `gpusim_submit()` copies each batch into a service-private buffer
  before `rcs_submit()` reads a field (copy, then check).

| File | What it is |
|---|---|
| `gpusim.h`, `gpusim.c` | Device: open/close, asset grants and polling, submit, scanout, client batch encoder |
| `gpusim_game.c` | 720p game workload and frame-time benchmark, all three tiers |
| `tier_env.c` | Picks the super-op tier from `$MXS_TIER` for the contract suite |
| `bench_cloud.txt` | Last benchmark output (cloud container) |
| `caps_frame.png` | Last frame of the `caps` scene, from the front buffer |

Change outside this folder: `contracts/gpu-commands-ref/rcs.c` gained an optional
`#ifdef RCS_USE_SUPEROPS` backend. Without the define it builds and behaves exactly
as before (`make test` there: 57 685 passed).

## Build and run

```
make B=/tmp/gpusim test    # contract suite on the SIMD backend x 3 tiers (ASan+UBSan), then 5 frames of the game under ASan
make B=/tmp/gpusim bench   # 300 frames per scene per tier, -O2, prints the table below
```

## Verified

- The reference contract suite (`rcs_test.c`, 57 685 checks: wire sizes, blend
  oracle, damage, envelope, rights, atomicity, slots, geometry, budgets, 80 000
  fuzz batches, surface hashes) passes unchanged on the super-op backend on the
  scalar, v128 and v256 tiers under ASan + UBSan. So the SIMD paths are
  bit-identical to the reference on everything that suite checks.
- The game workload runs 5 frames per scene per tier under ASan + UBSan with no
  findings and no rejected batch.

## Benchmark: 720p, one core, 30 fps target (33.3 ms)

Machine: cloud container, Intel Xeon @ 2.10 GHz, 4 vCPU, gcc 13.3 `-O2`. **Not the
user's PC**; the PC run is the next step. Each frame is client encode + service
submit (copy-out, validate, execute) + scanout. Unthrottled, so `fps` is what one
core could produce; `miss` counts frames over 33.3 ms out of 300.

Scenes (all at 1280×720 ARGB):

- **2d:** background copy blit from a bound asset, 300 alpha sprites 64×64 from an
  atlas asset, one 64 KiB HUD upload + alpha blit, 40 full-width lines, 20 rects.
  3.13 M pixel-ops per frame.
- **2d+3d:** the 2d scene plus 5 000 flat-shaded triangles (about 30 px each) from
  the mesh asset.
- **caps:** every per-frame cap at once: 590 alpha sprites (5.13 M pixel-ops after
  clipping, against the 6 M cap), 20 000 triangles (the cap), 32 uploads = 2 MiB
  (the cap), spread over 13 batches.

| tier | scene | mean ms | p99 ms | max ms | fps | misses / 300 |
|---|---|---|---|---|---|---|
| scalar | 2d | 6.87 | 9.21 | 10.02 | 145 | 0 |
| scalar | 2d+3d | 8.39 | 11.00 | 11.30 | 119 | 0 |
| scalar | caps | 19.76 | 26.20 | 38.14 | 51 | 1 |
| v128 (SSE2) | 2d | 4.48 | 5.88 | 7.11 | 223 | 0 |
| v128 (SSE2) | 2d+3d | 5.37 | 7.38 | 7.59 | 186 | 0 |
| v128 (SSE2) | caps | 16.89 | 23.03 | 29.81 | 59 | 0 |
| v256 (AVX2) | 2d | 3.41 | 4.57 | 5.14 | 293 | 0 |
| v256 (AVX2) | 2d+3d | 4.05 | 5.37 | 6.08 | 247 | 0 |
| v256 (AVX2) | caps | 12.78 | 18.22 | 25.47 | 78 | 0 |

Scanout of dirty tiles costs 0.3–0.65 ms of those totals; client encoding is
0.01–0.1 ms. Asset load (3 assets, 5.3 MB, SHA-256 verified) took 18.5 ms with a
warm page cache; a cold disk will be slower and was not measured.

Reading: a typical 2D game frame with a few thousand low-poly triangles uses
about 4–8 ms of a 33.3 ms frame on one core, so 30 fps holds with room for
game logic and encode. A game at every cap at once still fits on average on all
tiers (13–20 ms), but the scalar tier had one 38 ms frame in 300 and the p99 is
18–26 ms, which leaves too little for game logic (4–8 ms) plus encode (2–10 ms)
on the slower tiers.

## Where it falls short (plainly)

1. **It is not a Linux GPU.** This executes the RCS 2D/low-poly command set, not
   OpenGL, Vulkan or DRM/KMS. An unmodified Linux game or desktop cannot use it.
   Running "any Linux OS fully supported" graphics on it would need a driver that
   translates a real API (for example a Mesa/llvmpipe-style software renderer
   behind virtio-gpu) onto these commands, which is a separate, much larger piece.
2. **Triangles are flat colour only.** `texture` and `BAKED` shading are validated
   but not drawn (contract open point 4); there is no depth buffer (open point 3)
   and no transform, so meshes are drawn as stored. Real 3D will cost more than
   the numbers above.
3. **One core, no frame pacing.** The service runs on the caller's thread; there
   is no separate ring transport, no vsync and no compositor deadline. Worst-case
   frames at the caps (25–38 ms max) would miss a 30 fps deadline on a busy core.
4. **BIND is synchronous on resident data.** If an asset is not resident yet,
   BIND writes zeros and DRAW_TRIANGLES draws nothing (reference behaviour,
   contract open point 6); the demo waits for residency before drawing.
5. **Measured on the cloud container only**, not the user's i3-14100.
6. **Images must be linear** (`assetpack image`, not `--tiles`) with stride equal
   to width; the RCS asset table has no stride field. Meshes are blobs of whole
   28-byte triangles; anything else is refused at grant time.
