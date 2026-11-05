# matrixos/superops — SIMD super-op kernels

Wave 1 module. Owner: thread "Upgrade SIMD super-ops in compiler".
Contract it implements: `../contracts/gpu-commands.md` (op list, validation, blend
arithmetic) and security review finding 6 (`../reviews/security-review.md`).

## Files

| File | What it is |
|---|---|
| `mx_superops.h` | Public API: bounded spans, surfaces, pixel kernels, vector kernels, tiers, error codes |
| `mx_superops.c` | Entry points with bounds checks, scalar tier, tier dispatch, CRC tables |
| `mx_superops_kernels.inc` | Vector kernel bodies, compiled twice (128-bit baseline, 256-bit AVX2) via GCC/clang vector extensions |
| `mx_superops_ref.c` | Scalar references. Shares no code with the kernels (tests thread requirement) |
| `mx_superops_compat.h` | Baseline-shaped dispatcher (`mx_superop_dispatch2`) with the old return codes plus explicit capacities |
| `mx_superops_lower.h` | Compiler lowering: loop shape → kernel call, legality rules, fixed SIMD tier flags |
| `test_superops.c` | Unit tests (ASan + UBSan) |
| `bench_superops.c`, `bench.csv` | Anti-illusion benchmark rows, last run on the cloud container |
| `bench_tri_sweep.c`, `tri_sweep_cloud.csv` | Triangle row-span sweep (scalar vs SIMD vs default path) and the bench-backed path check, `make B=... perfcheck` |

Build and run: `make B=/tmp/superops-build test` and `make B=/tmp/superops-build bench`
(the project-files share has no execute permission, so build products go to `B`).
Tested with gcc 13.3 (ASan/UBSan) and clang 18 (no sanitizer runtime installed here).

## Contract the kernels enforce (finding 6)

Every entry point takes bounded buffers: `mxs_span {base, len}` in elements, or
`mxs_surface {span, w, h, pitch}`. Before any memory is touched:

- count 0, or rect `w <= 0 || h <= 0` → `MXS_E_ZERO_LEN`
- count > span, count > 2^31, `(h-1)*pitch + w` > span or > 2^31 → `MXS_E_OVERSIZE` / `MXS_E_SURFACE` (64-bit arithmetic, cannot wrap)
- base not aligned to the element → `MXS_E_ALIGN`; `pitch < w` → `MXS_E_PITCH`
- rect outside its surface → `MXS_E_RECT`; partial overlap of dst with a source → `MXS_E_OVERLAP` (exact in-place alias is allowed for element-wise ops, never for copy)
- blend mode ≥ 3 → `MXS_E_MODE`

On any error nothing has been written (tests check the buffers byte for byte).
The baseline's `gpu_vadd` truncation (7 bytes → 1 element, rc 0) is impossible
here because counts are elements; the compat shim refuses a byte count that is
not a multiple of 4 with `-4`.

The kernels assume the command set's validator already clipped rects to the
target and the clip region and checked formats; they re-check geometry against
the backing span anyway, so a validator bug becomes a refused call, not a write.

## Kernels

| Kernel | Semantics | Tiers | Notes |
|---|---|---|---|
| `mxs_fill_rect`, `mxs_clear` | FILL_RECT / CLEAR | scalar, v128, v256 | |
| `mxs_blit` COPY / ALPHA / ADD | BLIT | all | ALPHA and ADD follow `gpu-commands.md` "Blend arithmetic" bit for bit; `test_blend_oracle` holds the four `rcs_test.c` cases and an exhaustive channel sweep was run against `rcs.c blend_px` |
| `mxs_line` | LINE, Bresenham, clipped per pixel | scalar only | not a SIMD shape |
| `mxs_triangle` | DRAW_TRIANGLES flat, same inside test as `rcs.c` | all, with a span threshold | barycentric weights stepped incrementally; rows shorter than the per-tier threshold (128 px on v128, 96 px on v256) always run the scalar kernel, longer rows run 32-bit SIMD lanes when the triangle fits a 2^14 window, int64 lanes otherwise (see "Triangle span threshold"). Sampling rule for `texture`/`BAKED` is **not defined yet** (open point 4 of the contract) |
| `mxs_blit_dirty` | copy + per-pixel XOR shadow bit + changed count | all | optional for the canvas thread, which tracks tiles itself |
| `mxs_vadd_f32`, `mxs_vmul_f32`, `mxs_vfma_f32`, `mxs_vadd_sat_u8` | element-wise | all | VFMA is two roundings (`a*b` then `+d`); never fused, so it equals C under `-ffp-contract=off` |
| `mxs_reduce_sum_f32`, `mxs_dot_f32` | reductions | all | fixed 8-lane strided order, then a fixed tree; bit-identical on every tier. Differs from a sequential C sum, so the lowering only takes these under fast-math |
| `mxs_minmax_u8`, `mxs_crc32`, `mxs_fill8`, `mxs_copy` | compute super-ops | all | CRC-32 IEEE (zlib); slicing-by-8 on vector tiers, table-by-1 on scalar |

Tier selection: `mxs_set_tier(MXS_TIER_AUTO)` picks AVX2 if `__builtin_cpu_supports("avx2")`,
else the 128-bit baseline. The AVX2 kernels carry `__attribute__((target("avx2")))`, so one
binary runs on every x86-64 CPU and the build never needs `-march=native`. On aarch64 the
128-bit tier compiles to NEON (not run here: no ARM machine in this environment).

## Compiler lowering (`mx_superops_lower.h`)

`mxl_lower()` maps a normalised countable loop (unit stride, one statement) to a kernel:

| Source shape | Kernel | Refused when |
|---|---|---|
| `d[i] = c` (u8) | `mxs_fill8` | |
| `d[i] = a[i]` | `mxs_copy` | d aliases a (exactly or maybe) |
| `d[i] = a[i] + b[i]`, `* b[i]` (f32) | `mxs_vadd_f32`, `mxs_vmul_f32` | d may partially overlap a or b |
| `d[i] = a[i]*b[i] + d[i]` (f32) | `mxs_vfma_f32` | same |
| `d[i] = min(a[i]+b[i], 255)` (u8) | `mxs_vadd_sat_u8` | same |
| `acc += a[i]`, `acc += a[i]*b[i]` (f32) | `mxs_reduce_sum_f32`, `mxs_dot_f32` | not fast-math (order would change the result) |
| min/max over u8, CRC-32 update loop | `mxs_minmax_u8`, `mxs_crc32` | |
| known trip count > a known array length | — | kept scalar so the program's own diagnostic fires |

`mxs_tier_cflags(tier)` returns the fixed flags for the build profile:
scalar `-fno-tree-vectorize -ffp-contract=off`, v128 `-msse2 -ffp-contract=off`,
v256 `-mavx2 -mno-fma -ffp-contract=off`. This replaces `-march=native` in the
`runtime-fast` profile (CONTROL.md decision 3). The frontend thread owns the profile
table; this is the string it should use.

## Verified / not verified

Verified (run here, `make test`, 203 checks, 0 failures, ASan+UBSan clean):
- every kernel on scalar, v128 and v256 equals the independent scalar reference on
  random inputs, including lengths 1..70 (tails) and in-place forms;
- blend ALPHA/ADD equal `rcs.c blend_px` on the oracle cases and an exhaustive
  (a, s, d) channel sweep;
- every zero-length and oversize case per entry point is refused and leaves the
  buffers untouched, including a surface whose `(h-1)*pitch+w` would wrap 64 bits;
- 60 000 random requests on guarded buffers across all tiers: canaries intact,
  4 889 accepted (so the generator does reach the kernels);
- baseline regression values through the compat shim: `gpu_vadd {1..8}+{10..80}`
  gives `a[7]=88`, returns 8; source shorter → -3; 7-byte count → -4; `gpu_clear`
  0x1FF fills 0xFF; crc32 of 32×0x5A = 0x9139d123; 16-byte buffer with length 32
  → refused (-5) for crc32 and gpu_clear; missing GPU cap → -101 and untouched.

Not verified:
- aarch64/NEON build and results (no ARM host here);
- Windows/MSVC (vector extensions are GCC/clang only; MSVC needs an intrinsics port);
- behaviour under the real render service: `rcs.c` still runs its scalar loops
  until the command-set thread swaps them for these calls;
- benchmark numbers on the user's PC; the rows below are from a shared 4-vCPU
  cloud container (Xeon 2.8 GHz, AVX2 and AVX-512 present) and moved by up to
  ~40% between runs. Treat them as relative, not absolute.

## Benchmark (bench.csv, median of 21 runs after 3 warmups, output consumed)

| Metric | scalar | v128 | v256 | best vs scalar |
|---|---|---|---|---|
| vadd f32, 1M | 0.51 ms | 0.43 ms | 0.46 ms | 1.2× (memory bound, ~27 GB/s) |
| vfma f32, 1M | 0.59 ms | 0.44 ms | 0.45 ms | 1.3× |
| dot f32, 1M | 0.69 ms | 0.27 ms | 0.31 ms | 2.5× |
| saturating u8 add, 4 MB | 2.85 ms | 0.44 ms | 0.52 ms | 6.5× |
| crc32, 4 MB | 10.3 ms | 2.03 ms | 2.18 ms | 5× (slicing-by-8; the baseline's bitwise loop is ~8× slower than our scalar) |
| clear 720p | 0.18 ms | 0.13 ms | 0.17 ms | 1.4× |
| blit copy 720p | 2.35 ms | 0.29 ms | 0.29 ms | 8× |
| blit alpha 720p | 3.42 ms | 1.95 ms | 0.84 ms | 4× |
| blit add 720p | 2.75 ms | 0.35 ms | 0.31 ms | 9× |
| 200 alpha sprites 64×64 | 5.39 ms | 2.21 ms | 1.36 ms | 4× (design budget line: 2–4 ms) |
| blit + dirty shadow 720p | 1.01 ms | 0.35 ms | 0.26 ms | 3.9× |
| 20 000 flat triangles (20 px) | 13.7 ms | 13.7 ms | 13.7 ms | 1× by design: rows under 96 px take the scalar path on every tier (≈0.5 µs per triangle here, 0.37 µs on the PC; design budget 8–12 ms for 20 k) |

## Triangle span threshold

The lead's PC measurement (i3-14100, `../bench/pc/pc_superops_bench.csv`) showed the
reverse of the cloud box for 20 000 small triangles: 7.4 ms scalar, 10.4 ms v128, 11.4 ms
v256. Short rows pay the per-row vector setup and gain nothing. The fix: `mxs_triangle` runs
the scalar incremental kernel for any row shorter than a per-tier threshold
(`MXS_TRI_SIMD_MIN_SPAN_V128` = 128 px, `MXS_TRI_SIMD_MIN_SPAN_V256` = 96 px) and the SIMD
kernel otherwise; `mxs_set_tri_simd_min_span()` overrides both (0 forces SIMD, `UINT32_MAX`
forces scalar), `mxs_reset_tri_simd_min_span()` restores the defaults.

Why 96 and 128: the sweep below (cloud container, interleaved runs, median ns per row) shows
SIMD at best neutral below 64 px on both tiers. v256 wins 2 to 3.5× from 96 px. v128 at 96 px
was marginal: it won in my sweep but lost 1.3 to 1.5× in two of the tests thread's three runs
on the same container, and wins clearly from 128 px, so v128 starts at 128. The PC data covers
only the 20 px case, where scalar wins by 40%. The bench thread should run `make B=... tri-sweep` on the PC; if SIMD
already wins at 48 or 64 px there, lower the default.

| span px | v128 scalar | v128 SIMD | v256 scalar | v256 SIMD | default path |
|---|---|---|---|---|---|
| 8 | 44 | 60 | 46 | 58 | scalar |
| 20 | 53 | 52 | 56 | 76 | scalar |
| 32 | 66 | 74 | 56 | 38 | scalar |
| 64 | 120 | 193 | 128 | 143 | scalar |
| 96 | 167 | 119 | 184 | 87 | SIMD |
| 128 | 158 | 86 | 198 | 79 | SIMD |
| 256 | 282 | 167 | 289 | 83 | SIMD |
| 512 | 656 | 359 | 554 | 152 | SIMD |

Bench-backed check: `make B=/tmp/x perfcheck` builds `bench_tri_sweep --check`, which times
forced-scalar, forced-SIMD and the default path interleaved (11 runs, order rotated) and fails
when the default path is a wrong choice: at spans at or above the tier's threshold, if the chosen SIMD path is
slower than scalar by more than 25%; below it the chosen path is the scalar code itself, so
auto-vs-scalar is not compared (the tests thread showed that comparison is inside this shared
container's noise), and it fails instead if forced SIMD beats scalar by more than 2x, which
would mean the threshold is too high on that machine. It is a separate target, not part of the
sanitizer test build, because timings under ASan say nothing. Passes here in about a minute;
the PC run is the one that matters and is with the bench thread.

With the threshold the 20 000 small-triangle row runs the scalar kernel on every tier, so the
PC should see its 7.4 ms on all three. A span-walking rasteriser (edge stepping instead of a
bounding-box inside test) would cut the remaining per-row cost; not done in this wave.
