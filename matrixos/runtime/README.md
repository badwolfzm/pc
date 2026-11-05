# matrixos/runtime — game runtime: ring → validator → surfaces → dirty tiles → sink

Owner: thread "Connect game runtime and rendering". Status: **host prototype,
tested (229 checks, ASan/UBSan clean, gcc 13 and clang 18), benchmarked on the
cloud container only. Not yet run on the user's PC and not yet inside the
Warden kernel (the kernel has no loader yet; see "Kernel mapping").**

This folder is the first place the wave-1 modules are linked together. One
sandboxed game, in its own process, loads assets by slot from the
hash-verified store, draws through the RCS command ring, and the service
presents only the 16x16 tiles that changed to the encoder sink.

```
 game (child process)            render service (host runtime)                 sinks
 ┌───────────────┐  mx_ring   ┌──────────────────────────────────────────┐
 │ mx_rcs_build  │ ─────────▶ │ mx_ring_take_batch: copy out, bounds via  │
 │ waits on      │  results   │   mx_validate_len (frontend/mx_input.h)   │
 │ ready_mask    │ ◀───────── │ rcs_submit: validate + execute (rcs.c)    │   PPM / hash /
 └───────────────┘            │ PRESENT → damaged tiles → gc_damage_bits  │ ─▶ encoder
                              │   → gc_present (canvas) → sink            │
                              │ asset_poll → asset_map → rcs asset table  │ ◀─ asset_store
                              └──────────────────────────────────────────┘    (jailed, SHA-256)
```

## Files

| File | What it is |
|---|---|
| `mx_ring.h/.c` | The shared-memory command ring + result ring of gpu-commands.md section 3 (1 MiB data, 64 descriptors, 64 results, a 256-bit `ready_mask` of resident asset slots). The service reads every client-written field once, bounds it with `mx_validate_len` against sizes it owns, copies the batch out, then validates. A forged descriptor is answered with an envelope rejection and consumed, so it cannot wedge the ring. |
| `mx_render.h/.c` | The render service. Per client: ring, `rcs_client` (surfaces, asset table, budgets, rights), asset grants, display canvas. `mx_render_pump()` polls the store, refreshes the asset table pointers, drains each ring (at most 64 batches per client per pump, so a flooder cannot starve another client), and on PRESENT copies only the validator-damaged tiles of the presented surface into the ghost canvas and presents them. |
| `mx_rcs_build.h` | Header-only batch builder for the game side. The service never trusts what it built. |
| `test_runtime.c` | 229 checks (below). |
| `bench_runtime.c` | 720p service cost per frame, four scenes, evidence-rule output (`bench.csv`). |
| `game_demo.c` | The sample game: a fork()ed child with only the ring mapping and stdio; a 720p background and an alpha sprite streamed from a real store; 12 bouncing sprites, a HUD bar and a line; PPM snapshots of what the encoder received. |
| `Makefile` | `make OUT=/tmp/x test`, `asan`, `bench`, `demo`. Builds against `../contracts/gpu-commands-ref`, `../canvas`, `../assets`, `../frontend/include` as they are in project files (the repo copies of canvas and rcs are older than `rcs_present_info` / `gc_damage_bits`; sync first). |

## How the three modules connect

- **Assets reach the game by slot, never by name.** Policy calls
  `mx_render_grant_image(svc, client, slot, manifest_sha256, mip, priority)`
  (or `_grant_mesh`). The service opens and verifies the manifest, fills the
  rcs asset table entry with the geometry (an image granted as a mesh or a
  blob granted as an image is refused, `ASSET_ERR_FORMAT`), and queues the
  mip. When `asset_poll` reports it resident, `asset_map`'s pointer goes into
  the table and the slot's bit in the ring's `ready_mask` is set. A mapped
  length shorter than the declared geometry is treated as not resident. A
  chunk whose SHA-256 does not match never becomes ready (test: a corrupted
  chunk, 300 polls, bit stays clear, store counts the hash failure).
- **BIND_ASSET before residency** is accepted by the validator (as rcs.c
  defines it) and binds the zero stand-in; the game is expected to wait on
  `ready_mask`. Both halves are tested. If the lead prefers a synchronous
  refusal, it is a one-line change in `refresh_grants`/`composite` plus a new
  `rcs_error`, which belongs to the command-set thread.
- **PRESENT → canvas.** rcs.c records, per surface, the tiles every validated
  draw touched (`rcs_present_info`). The service copies exactly those tiles
  from the surface into the canvas `work` buffer, imports the bits with
  `gc_damage_bits`, and `gc_present` XOR-filters the ones whose pixels did not
  really change before calling the sink. Test: an 8x8 fill inside one tile
  emits one tile; redrawing it with the same colour emits zero tiles
  (`tiles_false` = 1). The first present of a display, or a display size
  change, is a key frame (`gc_invalidate_all`).
- **Isolation on the host.** The game is a child process holding the ring
  mapping only (store closed, fds 3+ closed, `PR_SET_NO_NEW_PRIVS`). It cannot
  reach surfaces, the canvas or the store. The service's copy of a batch is
  immune to the child rewriting the ring afterwards (tested). This is process
  isolation, not the Warden boundary; the kernel supplies that.

## Verified (this cloud container: 4 cores, Linux 6.18, gcc 13.3, clang 18)

`make OUT=/tmp/x test`: **229 passed, 0 failed**; `make asan`: same, no
ASan/UBSan reports; clang -O2: same.

| Group | Checks |
|---|---|
| ring | roundtrip; copy-out immune to later client writes; forged descriptors: over the batch cap, offset+len past the area, offset near UINT32_MAX (no wrap), shorter than a header, last 16 bytes still reachable; descriptor table caps in-flight at 64; 40 × 200 KiB batches survive the data-area wraparound in order (hash of in == hash of out) |
| service | create/upload/clear/blit/present; key frame emits all tiles; ghost == presented surface; one-tile draw emits one tile; identical redraw emits zero; rejected batch names the command, presents nothing, leaves no open frame, display hash unchanged; bad descriptor answered as `too_large` at index 0xFFFFFFFF and counted; ring works afterwards; a client forging `cmd_head` += 100000 is drained 64 per pump while a second client is served in the same pump; PRESENT without the `present` right → `right_missing`; detach revokes the ring (reserve fails) and frees everything |
| assets (real `assetpack` store) | grants by slot; wrong-kind grants refused; double grant refused; table filled from the manifest; bind-before-ready binds zeros; image and mesh become ready; bound surface bytes == packed image; display shows the asset (16 tiles for 64x64); two triangles from the streamed mesh land at the right pixels; corrupted chunk never ready, hash failure counted |
| two processes | child game, 60 frames, every frame accepted, 60 presents, no bad descriptors, after the key frame 2–6 tiles per frame; display hash equals an in-process replay; hostile child (one forged descriptor at frame 10) gets a rejection and continues, exactly one bad descriptor counted |

### Benchmark: service cost per 720p frame (`make bench`, cloud container, one run, NOT the user's PC)

Timer floor 25 ns. 5 warmup + 101 timed frames, median / p95. "service" =
copy-out + validate/execute + tile copy + ghost present + sink; the sink is an
encoder stand-in that FNV-hashes every delivered pixel (so output is
consumed; its cost is shown separately). Budget: 33,333 µs per frame at 30 fps.

| scene | what a frame does | service median µs | p95 µs | of which submit | of which composite (sink) | tiles | % of 33.3 ms (p95) |
|---|---|---|---|---|---|---|---|
| static | BEGIN/PRESENT, nothing drawn | 5.3 | 5.5 | 0.4 | 4.7 (0) | 0 | 0.0 |
| sprites16 | restore 16 × 64² from bg, 16 alpha sprites, HUD | 631 | 734 | 343 | 279 (122) | 356 | 2.2 |
| sprites16_lines200 | above + 200 long lines | 2,918 | 3,546 | 1,343 | 1,565 (903) | 2,528 | 10.6 |
| scroll_full | full-screen blit, every tile changes | 2,791 | 3,542 | 501 | 2,265 (1,260) | 3,600 | 10.6 |

`bench.csv` has the full rows with consumed hashes. Reading: with the scalar
reference executor (rcs.c, no super-ops yet) and a hashing sink, the service
side of a sprite game uses about 2 % of the 30 fps budget and a worst-case
full redraw about 11 %. The remaining ~90 % is for the game itself, the real
encoder, and the kernel's IPC, none of which are measured here.

### Demo (`make demo`): one game, real store, two processes

`game_demo <dir> 120` (uncapped) and `game_demo <dir> 90 30` (paced to 30 fps):

| | uncapped, 120 frames | capped 30 fps, 90 frames |
|---|---|---|
| frames presented / rejected | 120 / 0 | 90 / 0 |
| fork → assets verified and first present | 60 ms | 96 ms |
| achieved | 807 fps (bounded by the child's round trip through the polled ring) | 30.8 fps |
| service time per frame (copy, submit, composite) | 1.34 ms (0.001, 0.35, 0.98) | 2.23 ms (0.002, 0.64, 1.58) |
| tiles to the encoder per frame | 356 of 3,600 (356 KiB) | 366 of 3,600 |

The capped run is slower per frame because the service sleeps between frames
and comes back cold (cache and frequency); the uncapped run is the service's
own cost. Snapshots `frame_0001.ppm`, `frame_0030.ppm`, `frame_0060.ppm` are
written from the ghost (what the encoder saw); frame 60 shows the background
checkerboard, 12 alpha-blended sprites with restored background behind them,
the HUD bar and the sweeping line.

## Not verified

- **Not on the user's PC.** All numbers above are from this container. The
  demo and bench are plain C and run under WSL2 (`make OUT=/tmp/x demo`); the
  numbers should be replaced with that run.
- **Not in the kernel.** The game runs as a Linux child process. The kernel
  has address spaces and endpoints (Phase 2a) but no loader yet, so no game
  binary can run there. The ring layout is kernel-ready: see below.
- **Scalar executor.** rcs.c's reference loops execute the commands; the
  super-ops SIMD kernels are not wired in. On the numbers above the executor
  is at most 1.3 ms per frame, so wiring super-ops is an optimisation, not a
  budget need, at 720p.
- **No real encoder.** The sink hashes pixels. The bench thread's
  composite+convert+encode of 2.9–5.6 ms per full 720p frame on the PC comes
  on top of the service cost here; with ~360 dirty tiles per frame of a
  sprite game the encoder sees a tenth of the frame.
- Ring notification is polling (`nap()` of 100–200 µs when idle). Idle wake-up
  latency therefore adds up to 0.2 ms per round trip in the demo.
- Single service thread; the store's reader threads are the only concurrency
  and they were exercised under ASan with 2 readers, not under TSan.
- `MX_RENDER_MAX_CLIENTS` = 8, no per-client memory budget beyond the RCS
  surface-pixel cap (a real service charges the client's untyped memory).

## Kernel mapping (for the kernel thread, Phase 2b/3)

- The `mx_ring` struct is the content of the ring pages the kernel maps into
  both the game's and the service's address spaces (read-write for the game:
  it must write descriptors and data; the service trusts none of it).
- "Published" = `SYS_SEND` on the game's endpoint capability with w0 = the
  descriptor index; the service `SYS_RECV`s and runs `mx_ring_take_batch`.
  Results go back through the result ring and a send on the service's reply
  endpoint. The badge identifies the client.
- The `render` capability's attributes (gpu-commands.md section 7) are the
  arguments of `mx_render_attach` (`rights`) and the grants
  (`mx_render_grant_*`, from the manifest's asset list). Revoke =
  `mx_render_detach`.
- Nothing in `mx_ring.c`/`mx_render.c` needs libc beyond `memcpy`, `malloc`
  for the scratch copy and surfaces, and `clock_gettime` for stats, so the
  service can live in the render container as is.

## Next

1. Run `make test bench demo` on DESKTOP-9NPQNID under WSL2 and replace the numbers.
2. Swap the executor's scalar loops for `mx_superops` kernels (superops thread owns them; the service only calls them) and re-bench `scroll_full`.
3. Replace the polling wake-up with the kernel endpoint send/recv once the loader exists.
4. Hook the real encoder in place of the hash sink; the tile list is already in macroblock units.
