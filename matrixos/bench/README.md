# Memory bandwidth and per-core limits for the Warden render service (wave 0)

Measured 2026-11-05 on two machines. **The gating numbers are the PC run** (section below): the
user's PC DESKTOP-9NPQNID, Intel Core i3-14100, measured inside the Claude desktop Linux VM (Hyper-V,
1 core with 2 SMT threads, 3.9 GB RAM, AVX2, no AVX-512). Results in `pc/`. The first run, kept as a
labeled comparison, was a cloud container: 4 vCPU Intel Xeon (Sapphire Rapids class, KVM guest),
16 GB RAM, virtio SSD, no GPU, spec in `machine_cloud.json`. Both are VMs on real hardware, which is
the design's own deployment shape.

## PC run (DESKTOP-9NPQNID, i3-14100, one core at 4.5 GHz measured)

| Metric | PC (VM on i3-14100) | Cloud container | Design doc assumes |
| --- | --- | --- | --- |
| DRAM read, 1 core | 20.8 GB/s | 12.1 GB/s | 10 to 20 GB/s |
| DRAM write, 1 core | 13.5 GB/s | 7.8 GB/s | not stated |
| DRAM copy, 1 core | 19.7 GB/s | 10.7 GB/s | 10 to 20 GB/s |
| DRAM triad, 1 core | 24.0 GB/s | 12.9 GB/s | |
| DRAM copy / read / write / triad, both SMT threads of the one core | 27.5 / 37.4 / 20.2 / 31.9 GB/s | (4 cores) 40 / 36 / 28 / 48 | |
| L1 / L2 / L3 (16 MiB, over the 12 MB L3) / DRAM read bw | 73 / 69 / 24 / 21 GB/s | 171 / 154 / 30 / 15 | |
| L1 / L2 / L3 / DRAM load latency | 1.2 / 9.6 / 106 / 155 ns | 1.5 / 6.9 / 45 / 185 ns | |
| Peak FP32 FMA, AVX2 (harness row, compiler-limited lower bound) | 53 GFLOP/s | 36 (harness), 94 standalone | |
| 720p RGBA composite (3.7 MB memcpy), hot / from DRAM | 0.14 / 0.23 ms | 0.26 / 0.36 ms | about 0.5 ms |
| 200 alpha-blended 128x128 sprites (3.3 Mpix), AVX2 | 0.83 ms | 1.1 ms | 2 to 4 ms |
| RGBA to YUV420 at 720p, scalar C | 0.49 ms | 0.71 ms | folded into encode |
| H.264 720p, libx264 1 thread, ultrafast / veryfast, moving content | 2.2 / 4.9 ms | 2.9 / 6.3 ms | 6 to 10 ms |
| Shared-folder (Windows host disk through the VM's FUSE mount) sequential read / write | 266 MB/s / 10 MB/s | 3.2 GB/s / 1.1 GB/s (virtio) | SSD "about one second" per 100 to 200 MB |
| Shared-folder random 4 KiB reads | 4,800 IOPS, 208 us | 28,700 IOPS, 35 us | 50,000+ |
| VM's own disk (/tmp), direct write / read | 231 MB/s / 1.6 GB/s | | |
| 20,000 flat 20x20 triangles, superops `mxs_triangle`, scalar / v128 / v256 tier (`pc/pc_superops_bench.csv`) | 7.4 / 10.3 / 11.4 ms | 13.7 / 11.8 / 10.1 ms | 8 to 12 ms |

Conclusions from the PC run:

- The design doc's 10 to 20 GB/s per core is met at the top of the range. A single core on this CPU
  reads about 690 MB and writes about 450 MB per 33 ms frame. Bandwidth is not the bottleneck for any
  row in the doc's 30 fps table; compute per pixel and random-access latency are.
- Composite plus color conversion plus encode at ultrafast is about 2.9 ms per frame here, against the
  doc's 6.5 to 10.5 ms. At veryfast it is about 5.6 ms. The doc's render rows have 2 to 3x headroom.
- The L3 is small (12 MB) and slow relative to L2 here (106 ns, 24 GB/s). Tiles must fit L2, about
  1 MB on this part. A full 720p RGBA frame (3.7 MB) does not fit; process it in bands.
- Storage through the desktop VM's shared folder is slow (266 MB/s read, 10 MB/s write, 208 us per
  random read). That is the FUSE path of the Claude desktop VM, not the SSD itself, so it is not the
  right number for the design; the VM's own disk read 1.6 GB/s. A 100 to 200 MB scene swap over such a
  shared mount would take 0.4 to 0.8 s, close to the doc's "about one second". Over a direct virtio
  block or virtio-fs path it should be an order of magnitude faster; that was not measurable here.
- The superops triangle rasteriser meets the doc's 8 to 12 ms line at the scalar tier (7.4 ms, 22 percent
  of a 33 ms frame). On the PC the SIMD tiers are slower than scalar for these small triangles (short
  spans, per-span vector setup dominates); the cloud box showed the opposite order. Three repeat runs
  agreed within 5 percent on the median; v256 p95 reached 30 ms.
- Not visible from inside the VM: host RAM size and type, the physical disk model, and the other three
  cores. `pc/machine_pc.json` says what was captured and what has to be filled in from Windows.

## Cloud container run (labeled comparison)

Clock measured at 3.2 GHz under load (dependent add chain).

### Headline numbers (one core unless stated)

| Metric | Measured | Design doc assumes | Gap |
| --- | --- | --- | --- |
| DRAM read, 1 core (512 MiB stream) | 12.1 GB/s (pure loads: 15 GB/s) | 10 to 20 GB/s | inside the range, lower half |
| DRAM write, 1 core | 7.8 GB/s | not stated | writes are the weak side |
| DRAM copy, 1 core (counted both ways) | 10.7 GB/s | 10 to 20 GB/s | low end |
| DRAM triad, 1 core | 12.9 GB/s | | |
| DRAM copy / read / write / triad, 4 cores | 40 / 36 / 28 / 48 GB/s | | scales 3.4 to 3.8x on 4 cores; per-VM share on a 30-VM host not measurable here |
| L1 / L2 / L3 / DRAM read bandwidth | 171 / 154 / 30 / 15 GB/s | | L3 is 2x DRAM only; the working set must fit L2 (2 MB) to be fast |
| L1 / L2 / L3 / DRAM load latency | 1.5 / 6.9 / 45 / 185 ns | | random access to DRAM costs 185 ns per miss |
| Peak FP32 FMA, AVX-512 / AVX2 | 180 / 94 GFLOP/s | not stated | from standalone flops run at -O3; see caveats |
| 720p RGBA composite (3.7 MB memcpy) | 0.26 ms hot in L3, 0.36 ms from DRAM | about 0.5 ms | doc is conservative, 1.4 to 2x |
| 200 alpha-blended 128x128 sprites (3.3 Mpix), AVX2 src-over | 1.1 ms | 2 to 4 ms | doc is conservative, 2 to 4x; 2.9 Gpix/s blend rate |
| Per-pixel Lambert over full 720p, AVX-512 (16 B in/px) | 0.52 ms | "28 Mpix/s is most of the core" | bandwidth is not the limit; the doc's claim only holds for shaders of 100+ cycles per pixel (textures, several lights) |
| RGBA to YUV420 at 720p, scalar C | 0.71 ms | folded into "encode 6 to 10 ms" | ffmpeg swscale 1-thread path costs 3.2 ms; write our own |
| H.264 720p, libx264 1 thread, ultrafast | 2.9 ms moving, 2.0 ms mostly static | 6 to 10 ms | doc is conservative 2 to 3x at ultrafast |
| H.264 720p, libx264 1 thread, veryfast | 6.3 ms moving, 3.4 ms mostly static | 6 to 10 ms | matches at veryfast |
| Scene swap: 100 to 200 MB sequential from SSD | 3.2 to 3.9 GB/s direct, so 30 to 60 ms | about one second | 15 to 30x faster than assumed on this virtio SSD |
| Random 4 KiB reads, direct I/O | 28,700 IOPS, 35 us each | 50,000+ for SSD | below the doc's SSD figure; virtio adds latency |
| Page-cache read (shared asset store in host RAM) | 7.8 GB/s | | about 60 percent of DRAM copy speed; the read-only virtio-fs store will be slower than this |

Sum of the doc's 30 fps budget table rows that we could measure (composite + sprites + encode at
ultrafast + color conversion) is about 5 ms here against the doc's 8.5 to 14.5 ms. The two rows we
did not measure (low-poly software rasterizer 8 to 12 ms, game logic 4 to 8 ms) are the bulk of the
budget and are workload-dependent.

### What this means for the GPU command set budget

- Bandwidth per frame at 30 fps, one core: about 400 MB of reads or 260 MB of writes per 33 ms
  frame on this box. A full 720p RGBA frame is 3.7 MB, so pure traffic allows roughly 100 frame-sized
  passes per frame. Compute and latency, not bandwidth, will bound most commands.
- Working-set rule: anything that iterates per pixel should stream in L2-sized tiles (under 2 MB on
  this CPU, likely 1 MB or less on a desktop part). L3 gives only 2x DRAM here.
- Random access is the expensive mode: 185 ns per DRAM miss means about 180,000 dependent misses per
  frame. Sprite atlases and tile maps should be laid out for sequential access.
- Writes are the scarce resource (7.8 GB/s single core), so dirty-region compositing pays off twice:
  less encode work and less write traffic.
- Storage is fast enough that "GPU from storage" is realistic for sequential assets: 100 to 200 MB
  scene swaps cost tens of milliseconds, not a second. Random 4 KiB reads cost 35 us each, so
  per-frame asset fetches must be batched or prefetched, not demand-paged per sprite.

## Method (both machines)

`warden_bench.c` follows the MatrixOS baseline harness rules: 2 warmup runs, 11 timed runs, median and
p95 reported, timer floor measured (22 ns), every result consumed into a hash (printed in the CSV), one
evidence id per row. Single-core rows were pinned with `taskset -c 2`; the 4-thread rows used OpenMP
with `OMP_PROC_BIND=true`. Arrays of 512 MiB defeat the 260 MB L3. `run.sh` reproduces everything.

Files, cloud: `single_O3.csv` (memory, cache, FMA, composite, blend rows), `single_O2.csv` (same, and
the only valid Lambert row), `quad_O3.csv` (4-thread DRAM rows), `encode.txt` (ffmpeg command lines
and per-preset timings), `machine_cloud.json`. Files, PC: `pc/pc_single_O3.csv`, `pc/pc_single_O2.csv`,
`pc/pc_quad_O3.csv` (2 SMT threads), `pc/machine_pc.json`. The PC build used `-mavx2 -mfma` (no
AVX-512 on Raptor Lake), so the Lambert and AVX-512 FMA rows do not exist there. The harness takes an
optional part argument (mem, cache, fma, render) so each part fits a short command timeout; see run.sh.

## Caveats and what was not measured

- PC run: inside the Claude desktop Linux VM under Hyper-V with 1 core and 2 SMT threads, while
  Windows and the Claude app ran on the other cores. Not bare Windows and not WSL2. p95 is within
  10 to 15 percent of the median on most rows; the single-core DRAM read p95 is 60 percent above median.
- Cloud container, 4 shared vCPUs under KVM. Noisy neighbors are possible; p95 is within 10 to 25
  percent of the median on most rows.
- The design doc's tiering assumes a 512 MB VM. This box has 16 GB; memory size did not constrain
  any test. Nothing here measures 30 VMs sharing one host's memory bus.
- Peak FMA numbers come from a standalone -O3 build; in the harness, -O2 spills the accumulators and
  -O3 -ffast-math hoists the constant Lambert inputs (the 0.010 ms Lambert row in single_O3.csv is
  invalid, use single_O2.csv for it). The AVX2 FMA row in the harness is also compiler-limited;
  trust 94 GFLOP/s from the standalone run.
- Encode timings are single runs around ffmpeg with synthetic test-pattern content; real frames with
  texture will cost more. Hardware encoders, AV1 and VP9 were not measured.
- No GPU of any kind in this environment, so nothing about a real GPU path was measured.
- Storage numbers are for a virtio block device on cloud flash; a consumer NVMe will do more random
  IOPS and a SATA SSD less sequential throughput. virtio-fs itself was not measured.
