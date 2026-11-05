# Game runtime contract (thread 9: "Run Pong inside Warden", formerly "Connect game runtime and rendering")

Status: written against what runs on 2026-11-05. Two runtimes exist and this
file says which is which.

## 1. The frame loop (both runtimes)

    poll input -> step game -> draw into the ghost canvas (tiles marked) -> present changed tiles -> yield

- The canvas is matrixos/canvas (ghost_canvas.c). A draw marks 16x16 tiles; a
  present XOR-filters tiles whose pixels did not change and hands the rest to
  the sink. The game never touches the screen; the sink does.
- A frame always ends with a yield (kernel: `SYS_YIELD` refills the partition
  budget; a game that spends its whole budget without yielding is killed).
- Pacing: the game paces itself to the target rate from `rdtsc` calibrated
  against the partition timer (100 Hz ticks, `SYS_TICKS`). The kernel does not
  pace.

## 2. Runtime A: native guest on the Warden kernel (crates/warden-kernel/guest/pong/, src/pong.rs)

What the app holds: a console endpoint (WRITE) and one display object
(kind `Frame`). Syscalls 60..64 (range requested from the lead in CONTROL.md):

| nr | name | needs | does |
|---|---|---|---|
| 60 | DISPLAY_INFO | display READ | returns w \| h<<16 |
| 61 | PRESENT | display WRITE | copies rect (x\|y<<16\|w<<32\|h<<48) of a display-stride ARGB buffer in the caller's memory to the screen; every row's pages checked against the caller's mappings first |
| 62 | KEY | display READ | next PS/2 scancode or 0 (i8042 polled; no IRQ) |
| 63 | TICKS | display READ | partition timer ticks |
| 64 | REPORT | console WRITE | prints `report <thread> <key> <value>` |

Permissions in warden-abi terms: `render` (the display object), `clock=coarse`
(TICKS; `rdtsc` is also readable from ring 3 since CR4.TSD is clear), input
(KEY; no ABI name yet, folded into `render` for now). No `fs.read`: assets are
compiled into the app. No `random`: the app seeds from `rdtsc`.

Memory: code R-X at 0x800000 (256 KiB max), data/bss/heap RW+NX
0x840000..0x9E0000, stack 0x9F0000..0xA00000. All inside the 2 MiB user
window; a 320x240x32 canvas (work + ghost = 600 KiB) fits, 640x480 does not
until the window grows (thread 3/6).

## 3. Runtime B: host prototype (project files matrixos/runtime/)

The full MatrixOS path: shared-memory command ring -> RCS validator
(contracts/gpu-commands-ref) -> surfaces -> damage tiles -> ghost canvas ->
sink, assets granted by slot from the hash-verified store. The game is a
Linux child process. It runs under `warden run` (Phase 0 supervisor) with the
manifest below; the kernel path (runtime A) does not yet carry RCS because it
needs shared frames between address spaces.

    fs.read  <build dir>      fs.write <build dir>     exec <build dir>
    exec /usr/bin   exec /bin   proc.spawn   fs.read /etc   fs.read /proc   fs.read /dev   fs.write /dev

(`proc.spawn` for the game child and the store's reader threads; `/etc` for
the loader; `/dev` for /dev/null.) Observed under the supervisor on the cloud
box: `clone3` is reported "not implemented" and glibc falls back to `clone`,
which the supervisor allows with `proc.spawn`; `system()` for assetpack needs
`exec /bin` and fs.read on every PATH entry (the Phase 0 thread owns that
behaviour).

## 4. The 30 fps budget (33.3 ms per frame)

Measured pieces (sources named; none on the user's PC except the bench rows):

| piece | measured | where |
|---|---|---|
| present syscall path, 300 tiles of 16x16x32 (whole 320x240 screen) | < 1 ms per frame | runtime A, QEMU TCG cloud |
| gameplay frame with 2..6 dirty tiles | 34 us | runtime A, QEMU TCG cloud |
| RCS validate + execute, 720p sprite game (16 sprites, HUD) | 0.34 ms | runtime B, cloud |
| RCS validate + execute, 720p full-screen blit | 0.5 ms | runtime B, cloud |
| tile copy + ghost present + hashing sink, 720p full redraw | 2.3 ms (1.3 ms of it the hash) | runtime B, cloud |
| composite + convert + encode, 720p | 2.9 to 5.6 ms | matrixos/bench/pc (user's PC, Hyper-V VM) |

Split adopted for a 720p sprite game: game logic 8 ms, RCS + draw 2 ms,
canvas present 1 ms, encode 6 ms, IPC/syscalls 1 ms, headroom 15 ms. For
Pong at 320x240 inside the kernel the whole frame is under 2.5 ms worst case
(phase A worst frame), so the 30 fps slot is 93 % idle.

## 5. Open

- Shared frames between address spaces (thread 3/6) so the render service can
  be its own guest app and runtime A carries RCS.
- A `render` permission entry in warden-abi.json naming the display object
  (kernel thread).
- Input as a separate permission (`input=keyboard`) rather than a right on
  the display object.
