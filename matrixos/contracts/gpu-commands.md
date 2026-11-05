# Render service command set (RCS) — contract v1

Status: contract, wave 0. Owner: thread "Design GPU command set from storage".
Reference implementation and tests: `gpu-commands-ref/` (rcs.h, rcs.c, rcs_test.c, Makefile).
Also published as the doc "Render service command set" (https://claude.ai/artifact/RVqApBp6n99scL9U2eZGPp).

The render service is the Warden container that owns the memory-based GPU
(design doc, "MatrixOS compiler and the memory-based GPU"). A game never
touches pixels. It holds a `render` capability, writes fixed-size commands into
a shared-memory ring, and names everything by slot: surfaces it created, and
assets the policy container granted it from the storage container's read-only
asset store. The service copies each batch out of the ring, validates every
command against limits it owns, and only then executes. A batch is all or
nothing.

What this contract fixes: the command list, the wire format, the validation
rules, the blend arithmetic, the per-frame budgets and what a `render`
capability grants. What it leaves to other threads: the SIMD kernels that
execute the commands (superops), damage tracking and presentation (canvas),
how assets reach the service (assets), the numbers' final values (bench).

Every number in section 6 is labelled **assumed** (from the design doc) or
**measured** (bench thread, cloud container, 2026-11-05). None has been
measured on the user's PC yet.

---

## 1. Model

```
 game process                      render service container
 ┌──────────────────┐  command ring  ┌─────────────────────────────┐   asset
 │ MXSB binary      │ ───────────▶   │ copy out → validate → execute│ ◀──── storage
 │ holds `render`   │  result ring   │ surfaces[256]  assets[256]   │  (read-only
 │ capability       │ ◀───────────   │ per-frame budgets, clip, fence│   asset store)
 └──────────────────┘                └──────────────┬──────────────┘
                                                    │ PRESENT (surface slot)
                                                    ▼
                                           compositor (dirty-bit canvas) → encoder
```

- **Slots, not pointers.** A surface slot is an index 0..255 into the client's
  own table; an asset slot is an index 0..255 into the table policy filled from
  the manifest. No address, path, hash or string ever crosses the ring.
- **Receiver-owned sizes.** The ring descriptor says how many bytes arrived;
  the service checks the batch's own `total_bytes` against that, not the other
  way round. Every cap in section 5 is a constant in the service.
- **Copy, then check.** The batch is copied out of the ring before any field is
  read, so the sender cannot change it between check and use.
- **Two passes, atomic.** Pass 1 validates every command against a shadow copy
  of the client's bookkeeping (slots, budgets, frame state, rights), mutating
  only the shadow. Pass 2 executes. If any command fails, nothing executes and
  the client's state is exactly as before; the result names the first failing
  command and the reason.
- **Two batch kinds.** A RESOURCE batch holds only resource commands (create,
  destroy, bind, upload). A FRAME batch holds only draw and frame commands.
  Mixing is rejected (`wrong_class`). This keeps table mutation and drawing in
  separate validation domains.

## 2. Command set

14 commands. Body sizes are exact and fixed; only UPLOAD carries a payload.
"Cost" is what the validator charges against the frame budgets of section 5.

| Op | Name | Class | Right | Body (bytes) | Arguments | Cost charged |
|---|---|---|---|---|---|---|
| 0x01 | CREATE_SURFACE | resource | `resource` | 16 | slot u32, w u32, h u32, format u32 | w·h against the surface budget |
| 0x02 | DESTROY_SURFACE | resource | `resource` | 4 | slot u32 | frees w·h |
| 0x03 | BIND_ASSET | resource | `asset` | 8 | asset_slot u32, surface_slot u32 | w·h·bpp against the bind budget |
| 0x04 | UPLOAD | resource | `upload` | 20 + w·h·bpp | slot u32, rect (x,y,w,h i32), payload | payload bytes against the upload budget |
| 0x10 | SET_CLIP | draw | `draw` | 20 | target u32, rect | 0 |
| 0x11 | CLEAR | draw | `draw` | 8 | target u32, color u32 | region area (px-ops) |
| 0x12 | FILL_RECT | draw | `draw` | 24 | target u32, rect, color u32 | clipped area |
| 0x13 | BLIT | draw | `draw` | 40 | target u32, dx i32, dy i32, src u32, src rect, blend u32, flags u32 (=0) | clipped area ×1 (copy) or ×2 (alpha, add) |
| 0x14 | LINE | draw | `draw` | 24 | target u32, x0,y0,x1,y1 i32, color u32 | max(\|dx\|,\|dy\|)+1 |
| 0x15 | DRAW_TRIANGLES | draw | `mesh` | 24 | target u32, mesh_slot u32, first_tri u32, tri_count u32, texture u32 (or NONE), shading u32 | tri_count against the triangle budget |
| 0x20 | BEGIN_FRAME | frame | `present` | 4 | frame_no u32 | resets the per-frame budgets |
| 0x21 | PRESENT | frame | `present` | 8 | target u32, frame_no u32 | 0 here; hands the surface to the compositor |
| 0x22 | FENCE | frame | `present` | 4 | token u32 | ≤16 per batch |
| 0x23 | PROMISE_DROP | frame | none | 4 | rights_mask u32 | 0; one-way |

Enumerations: format `0 ARGB8888`, `1 A8`. Blend `0 COPY`, `1 ALPHA`, `2 ADD`.
Shading `0 FLAT`, `1 BAKED`. `NONE = 0xFFFFFFFF`. Colours are `0xAARRGGBB`.
Rect is four `int32` (x, y, w, h). Mesh vertices come from the asset store as
7 × int32 per triangle: three (x, y) in 16.16 fixed point, then a colour; the
asset's declared `tri_count` bounds `first_tri + tri_count`.

Mapping from the MatrixOS prototype: `gpu_clear` → CLEAR; `gpu_vadd`,
`crc32`, `memcpy` are compute super-ops, not render commands, and stay with the
superops thread (opcode range 0x30..0x3F is reserved for them if they are ever
exposed on this ring). The canvas prototype's `pset/rect/line/blit_checker` map
to FILL_RECT (1×1), FILL_RECT, LINE and BLIT.

## 3. Wire format

Little-endian, packed, no padding. Sizes are checked by `_Static_assert` in
`rcs_test.c`.

```
batch header (16)   magic u32 = 'RCSB' (0x42534352) | version u8 = 1 | kind u8 (1 RESOURCE, 2 FRAME)
                    | reserved u16 = 0 | cmd_count u32 | total_bytes u32
command header (16) opcode u16 | version u8 = 1 | flags u8 = 0 | body_len u32 | seq u32 | reserved u32 = 0
body                fixed size from the table above; UPLOAD: 20 + payload
result record (40)  magic u32 = 'RCSR' | status u32 (0 OK, 1 REJECTED) | failing_index u32 (0xFFFFFFFF = envelope)
                    | error u32 | pixel_ops u64 | triangles u32 | upload_bytes u32 | fences_done u32 | bind_bytes u32
```

Ring sizing (recommendation for the host runtime): command ring 1 MiB (four
maximum batches in flight), result ring 64 × 40 bytes. One ring pair per
client, created by the host and granted as a capability; the client cannot
create or resize it.

## 4. Validation rules

The service applies these in order and stops at the first failure. Error
names are the `rcs_error` enum in `rcs.h`.

**Envelope** (`failing_index = 0xFFFFFFFF`)
1. Delivered length ≤ 256 KiB (`too_large`), ≥ 16 (`length_mismatch`).
2. Magic, version, kind, reserved == 0 (`bad_magic`, `bad_version`, `bad_kind`, `reserved_nonzero`).
3. `total_bytes` equals the delivered length (`length_mismatch`).
4. `cmd_count` ≤ 4096 (`too_many_cmds`).
5. After the last command the offset equals the end: no trailing bytes (`length_mismatch`).

**Per command header** (`failing_index = i`)
6. 16 bytes remain for the header, and `body_len` bytes remain after it (`length_mismatch`).
7. `version == 1`, `flags == 0`, `reserved == 0`, `seq == i` (`bad_version`, `reserved_nonzero`, `bad_seq`).
8. Opcode is one of the 14 (`bad_opcode`) and belongs to the batch's class (`wrong_class`).
9. `body_len` equals the fixed size; UPLOAD: fixed ≤ body_len ≤ fixed + 64 KiB (`bad_body_len`).

**Authority**
10. The client's current rights (as narrowed by earlier PROMISE_DROPs, including ones earlier in this batch) include the opcode's right (`right_missing`).

**Slots**
11. Surface slot < 256 (`slot_range`); CREATE needs it empty (`slot_occupied`), everything else needs it live (`slot_empty`). Asset slot < 256 (`slot_range`), granted (`slot_empty`), and of the right kind: IMAGE for BIND, MESH for DRAW_TRIANGLES (`asset_kind`).
12. BLIT `src != target`; DRAW_TRIANGLES `texture != target` (`same_surface`).

**Geometry** (all arithmetic in 64 bits; no wrap is possible)
13. CREATE: 1 ≤ w, h ≤ 4096 (`bad_dim`); format ∈ {0, 1} (`bad_format`).
14. Rects that must lie inside a surface (UPLOAD rect in its slot, BLIT source rect in `src`, SET_CLIP rect in `target`): w, h > 0 (`rect_empty`), x, y ≥ 0 and x+w ≤ W, y+h ≤ H (`rect_outside`).
15. Rects that are clipped (FILL_RECT, BLIT destination): w, h > 0 (`rect_empty`); any position is legal, the drawable region is the intersection with the clip (or the whole target). Fully clipped is legal and costs 0.
16. LINE endpoints may lie anywhere; only pixels inside the region are written. The cost is the full span, so a span across int32 is rejected by budget, not by range.
17. BLIT: `blend` < 3 (`bad_blend`), `flags == 0` (`reserved_nonzero`), source and target formats equal (`format_mismatch`), alpha and add only on ARGB8888 (`bad_blend`).
18. BIND: surface w, h equal the asset's declared w, h (`bad_dim`) and formats match (`format_mismatch`). The client never states an asset's size; the asset table does.
19. UPLOAD: payload length is exactly w·h·bpp (`bad_body_len`) and ≤ 64 KiB.
20. DRAW_TRIANGLES: `tri_count ≥ 1` and `first_tri + tri_count ≤ asset.tri_count` in 64-bit (`mesh_range`); `shading` < 2 (`bad_shading`); target, and texture if given, are ARGB8888 (`format_mismatch`).
21. PRESENT target is ARGB8888 (`format_mismatch`).

**Frame state**
22. Draw commands and PRESENT need an open frame (`not_in_frame`); BEGIN_FRAME needs none (`already_in_frame`).
23. PRESENT `frame_no` equals the open frame's; each BEGIN_FRAME `frame_no` is greater than the last presented one (`frame_no`). A frame may span several batches.
24. SET_CLIP applies to one target until the next SET_CLIP, DESTROY of that target, or PRESENT.

**Budgets** (section 5; `*_budget`)
25. Sum of w·h over live surfaces ≤ 8 Mpx. Per frame: UPLOAD ≤ 2 MiB, BIND ≤ 8 MiB, pixel-ops ≤ 6 M, triangles ≤ 20 000. Per batch: FENCE ≤ 16.

**Executor contract.** The executor asserts every rule it relies on (slot
live, rect inside, counts agree with the validator's shadow). With
`-fsanitize=address,undefined` any gap between validator and executor is a
test failure, not a silent out-of-bounds write.

**Blend arithmetic (normative, shared with canvas and superops).**
ALPHA, per colour channel with `a` = source alpha: `t = s·a + d·(255−a) + 128;
out = (t + (t >> 8)) >> 8`; destination alpha unchanged. ADD: saturating add on
all four bytes. COPY: source replaces destination. The SIMD kernels must match
this bit for bit; `test_blend_rules` in `rcs_test.c` is the oracle.

## 5. Limits the service owns

| Constant | Value | Why this value |
|---|---|---|
| RCS_MAX_BATCH_BYTES | 256 KiB | Four fit in a 1 MiB ring; bounds the copy-out per batch |
| RCS_MAX_BATCH_CMDS | 4096 | 4096 × 56 B max ≈ 224 KiB, fits the batch cap |
| RCS_MAX_SURFACES / ASSETS | 256 each | Table copy for the shadow is ~5 KiB |
| RCS_MAX_DIM | 4096 | 4096² ARGB = 64 MiB, above the per-client pixel budget on its own |
| RCS_MAX_SURFACE_PIXELS | 8 Mpx (32 MiB ARGB) | ~8 × 720p: two frames, depth, atlases; inside the design's 512 MB plan |
| RCS_MAX_UPLOAD_BYTES | 64 KiB | One 128×128 ARGB tile; keeps a command bounded |
| RCS_FRAME_UPLOAD_BUDGET | 2 MiB | Dynamic content only; baked content comes via BIND |
| RCS_FRAME_BIND_BUDGET | 8 MiB | Two 720p backgrounds per frame |
| RCS_FRAME_PIXEL_BUDGET | 6 M px-ops | ≈ 6.5 full 720p writes; matches the composite + sprite rows below |
| RCS_FRAME_TRIANGLE_BUDGET | 20 000 | The design doc's low-poly row |
| RCS_MAX_FENCES_PER_BATCH | 16 | Bounds notifications per batch |

## 6. Bandwidth budget

Frame = 33.3 ms at 30 fps, 720p (921 600 px, 3.69 MB ARGB), one core.
**Assumed** = design doc (10 GB/s per core). **Measured** = bench thread on
the cloud container (4 vCPU Sapphire Rapids KVM guest; one core: read 12,
write 7.8, copy 10.7 GB/s; 200 alpha sprites 128² AVX2 in 1.1 ms; 3.7 MB
composite 0.26–0.36 ms). Not yet measured on the user's PC.

Bytes moved per cost unit:

| Command | Bytes per unit | Note |
|---|---|---|
| CLEAR, FILL_RECT | 4 B write per px | streaming store |
| BLIT copy | 8 B per px (4 read + 4 write) | 1 px-op |
| BLIT alpha / add | 12 B per px (read src, read dst, write) | 2 px-ops, so 6 B per op |
| LINE | 4 B write per step, worst one cache line (64 B) per step | scattered |
| DRAW_TRIANGLES | compute-bound: edge setup + ~50 px fill per low-poly triangle (assumed) | ~0.5 µs/tri assumed |
| UPLOAD | 3× payload (ring copy-out 2×, surface write 1×) | |
| BIND_ASSET | 2× asset bytes (read store buffer, write surface) | storage read is the assets thread's cost, off this core |
| CREATE_SURFACE | 4 B write per px, once | zero-fill; not per frame |
| others | 0 | |

Per-frame ceilings as enforced, and what they cost if a game hits them:

| Budget | Cap | Worst-case traffic | Time, assumed 10 GB/s | Time, measured | Design-doc row it maps to |
|---|---|---|---|---|---|
| Commands | 4096/batch, ≤256 KiB/batch | ~0.25 MB per batch; a typical frame ~500 cmds ≈ 20 KiB | < 0.03 ms | < 0.03 ms | — |
| 2D pixel-ops | 6 M | 48 MB (all copy) / 36 MB (all alpha) | 4.8 ms | 4.5 ms copy; ~1.0 ms if all alpha sprites at 2.9 Gpx/s | composite 0.5 ms + sprites 2–4 ms |
| Triangles | 20 000 | ~1 M px written ≈ 8 MB; compute-bound | 8–12 ms (assumed) | not measured | low-poly 3D 8–12 ms |
| UPLOAD | 2 MiB | 6 MiB | 0.6 ms | ~0.5 ms | part of streaming headroom 4 ms |
| BIND_ASSET | 8 MiB | 16 MiB | 1.6 ms | 0.8 ms (copy 10.7 GB/s) | part of streaming headroom 4 ms |
| **Render side at every cap** | | ~80 MB | **15–19 ms** | **~7 ms + triangles** | composite + sprites + 3D + streaming = 15–20.5 ms |

Remaining frame for the other rows (design doc): game logic 4–8 ms, encode
6–10 ms (measured: x264 ultrafast 2.0–2.9 ms, veryfast 3.4–6.3 ms). A game at
every render cap simultaneously plus the top of the logic and encode rows
overruns 33 ms by ~4 ms on the assumed numbers and fits on the measured ones.
The caps are per-resource ceilings, not a promise that all can be hit at once;
the compositor's frame deadline remains the backstop (canvas thread).

Ring traffic at 30 fps: commands ≈ 0.6 MB/s typical, ≤ 7 MB/s at 4096
commands per frame; UPLOAD ≤ 63 MB/s. Both are small against the 10 GB/s core.

Storage side (for the assets thread): a BIND never reads disk on the frame
path. Assets are requested ahead by the client via BIND of a future-scene
surface and complete asynchronously (FENCE); the measured 3.2–3.9 GB/s
sequential read puts a 100–200 MB scene swap at 30–60 ms, versus "about a
second" assumed in the design doc.

## 7. What a `render` capability grants (for the kernel ABI, additive v0.2)

The Phase 0 ABI (`abi/warden-abi.json`) has `load`, `fs.read`, `fs.write`,
`exec`, `net.connect`, `net.listen`, `proc.spawn`. `render` is a new permission
with the same shape (name, scope, doc):

- **Objects.** Exactly one command ring and one result ring to the render
  service (shared-memory rings created by the host runtime, 1 MiB and 2.5 KiB),
  plus one notification for fences and results. The client maps only the ring
  pages. No surface memory is ever mapped into the client: pixels live in the
  render service, and the client names them by slot. (A zero-copy upload
  mapping, `render.map`, is deliberately not in v1.)
- **No scope string.** In the ABI vocabulary `render` is a scope-less permission (kernel thread ruling, ABI v0.2): the grant parser takes a scope as one token, and these are not that. They are the `render` table of `warden.toml`, and they become attributes of the ring capability object the host runtime mints, as a memory budget is an attribute of an untyped-memory object: `[render] rights = [draw, upload, present, resource, asset, mesh]`, `surfaces.max_pixels = 8M`, `assets = [<asset store paths or hashes>]`. `rights` maps 1:1 to section 2's right bits; omitted rights are not granted. `assets` names what policy loads into the client's asset table from the storage container; the game therefore needs no `fs.read` on the asset store. `surfaces.max_pixels` may only be lowered from the service default. This section is the normative definition of those attributes; the ABI file only names the permission.
- **Attenuation.** `mint` can drop right bits and lower budgets, never add. `PROMISE_DROP` is the runtime equivalent, one-way. `revoke` on the parent capability tears down the rings; in-flight batches fail with a capability fault, and the service frees the client's surfaces.
- **MXSB.** `caps_required` gets a `render` bit; the loader refuses a binary whose manifest lacks `render` while the bit is set, as it does for the others.
- **Audit.** Every rejected batch is logged with the client badge, failing index and error name; policy can rate-limit a client that is rejected repeatedly.

## 8. Tests

`gpu-commands-ref/rcs_test.c`, built with `-std=c11 -Wall -Wextra -Werror
-Wconversion -fsanitize=address,undefined` (gcc 13) and also with clang 18
`-O2`. Run: `make BUILD=/tmp/rcs-build test` (the project-files mount is
noexec). Result on 2026-11-05: **57 669 checks passed, 0 failed**, both
compilers, no sanitizer findings, 1.1 s.

| Test | What it proves |
|---|---|
| wire sizes (`_Static_assert`) | every struct size in section 3 |
| happy path and determinism | create, upload, clear, fill, alpha blit, line, fence, present; exact pixel-op charge; two fresh clients produce identical surface hashes |
| blend rules | ALPHA matches the exact-round formula on four cases incl. a=0 and a=255; dst alpha unchanged; ADD saturates |
| envelope | too large, short, bad magic/version/kind/reserved, total_bytes mismatch, trailing bytes, truncated body, too many commands, cmd_count beyond bytes, bad seq, bad opcode, bad body_len, flags, cmd version, wrong class both ways; state unchanged after each |
| rights and atomicity | missing right rejected at the exact index with no side effects, including a BEGIN_FRAME earlier in the same batch; PROMISE_DROP narrows later commands in the same batch, is rolled back with a rejected batch, and is one-way once committed; reserved bits rejected; zero-rights client can do nothing |
| slots and resources | slot range/occupied/empty, bad dim, max dim, bad format, surface pixel budget, destroy-then-create in one batch, double destroy rolls back the whole batch; asset kind/empty/range, bind dim and format mismatch; mesh range incl. u32 wrap, zero count, kind, shading, same surface, A8 texture/target |
| geometry | INT32_MAX/INT32_MIN rects clip to zero cost without overflow; partial clip charges the visible area; empty/negative rects; BLIT source outside, dst far outside, same surface, format mismatch, bad blend, flags, empty src slot, slot range; A8 copy ok, A8 alpha rejected; SET_CLIP inside/outside/empty and clipping of CLEAR; LINE span cost and int32 span rejected by budget; UPLOAD payload exact, rect outside incl. INT32_MAX and negative, 64 KiB cap, A8 bpp |
| frame state and budgets | draw/present outside a frame, double begin, wrong present frame_no, A8 present, non-increasing frame_no, frame across batches; 6 full clears fit and the 7th is rejected; budget resets at BEGIN_FRAME; 20 000 triangles fit and 1 more is rejected; 5 × 64 KiB uploads exceed the batch cap; 32 × 64 KiB fill the 2 MiB frame budget and the 33rd is rejected; 2 × 720p binds fit and the third is rejected; 17 fences rejected |
| fuzz (80 000 batches) | 20 000 random byte strings and 60 000 well-formed envelopes with random bodies: no crash, no sanitizer finding; 2 529 batches accepted and executed; 27 of the 34 error codes reached (unreached: too_large, right_missing and the five budget codes that need large payloads or many commands; all seven are covered by the directed tests above) |

Not tested here (other threads): real SIMD kernels, damage tracking, the
compositor's deadline, asset streaming latency, the ring transport itself.

## 9. Open points

1. **Numbers.** All times in section 6 are assumed or measured on the cloud
   container. The bench thread will rerun on the user's PC; caps may move.
2. **Triangle cost.** 20 000 triangles per frame is the design's number; the
   per-triangle cost of the superops rasterizer is unmeasured. If it comes in
   under 0.25 µs the cap can double.
3. **Depth.** v1 has no depth buffer command; DRAW_TRIANGLES draws in order.
   A `DEPTH` surface format and a `depth` field would be an additive v2.
4. **Textured triangles.** `texture` and `BAKED` shading are validated but the
   reference draws flat colour; the superops kernel defines the sampling rule.
5. **Zero-copy upload.** Rejected for v1 (a mapped region is an attack
   surface); revisit if UPLOAD traffic ever matters.
6. **Asynchronous BIND.** The reference completes BIND synchronously. The real
   service completes it when the assets thread delivers the bytes; a draw from
   a surface whose bind is pending reads the previous contents. Whether that
   should instead be rejected (`asset_pending`) is the assets thread's call.
