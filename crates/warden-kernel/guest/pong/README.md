# Pong inside Warden

A native Warden guest app: Pong runs in ring 3 in its own address space on the
Warden microkernel under QEMU, draws on the emulated VGA through a
capability-gated display syscall, reads the PS/2 keyboard, and shares the
machine with three hostile threads that try to interfere and cannot.

    tests/kernel/pong.sh            # build with --features pong, run headless, inject keys, exit 0 when ALL HELD
    SHOW=1 tests/kernel/pong.sh     # same, with the QEMU window open (gtk; DISPLAY_BACKEND=sdl to change)

## What runs

| Thread | Code | Holds | Expected end |
|---|---|---|---|
| `pong` | `guest/pong/pong.c` + `rt.c` + `matrixos/canvas/ghost_canvas.c`, compiled freestanding into two flat blobs (text R-X at 0x800000, data RW+NX at 0x840000, heap to 0x9E0000, 64 KiB stack) | console endpoint (WRITE), display object (READ, WRITE) | `exited(0)` after three timed phases |
| `hostile writer` | ring-3 Rust in `src/pong.rs` | console only | tries PRESENT through an empty slot and through the console capability, floods 20 000 PRESENTs, then reads Pong's framebuffer address: `#PF` (nothing is mapped there in its space) |
| `hostile forger` | same | console only | tries to mint a display capability from the console (no GRANT), names every slot 0..15: all refused; exits 0 |
| `hostile spinner` | same | console only | infinite loop, budget 5 ticks: killed by the partition timer |

The game: W/S or arrow keys move the left paddle, the right paddle follows the
ball, Q quits. Rendering goes through the MatrixOS ghost canvas: draws mark
16x16 tiles, `gc_present()` hands only the changed tiles to `SYS_PRESENT`.

## Kernel side (`src/pong.rs`, only with `--features pong`)

- Display: Bochs/QEMU VGA (PCI 1234:1111, class 03) found by a config-space
  scan, 320x240x32 linear framebuffer set through the dispi ports, mapped
  supervisor-only into every address space (2 MiB pages, cache-disabled). No
  thread can reach a pixel except through the syscall. Without a VGA device
  the kernel presents into a private buffer and the checks still run.
- Keyboard: i8042 output buffer polled inside `SYS_KEY`. No interrupt line is
  unmasked; the partition timer is still the only IRQ.
- Syscalls 60..64 (`DISPLAY_INFO`, `PRESENT`, `KEY`, `TICKS`, `REPORT`).
  60..63 need a capability to the one display object (kind `Frame`): READ for
  info, keys and ticks, WRITE to present. `PRESENT` takes the caller's buffer
  address and a packed rect; every row's pages are checked against the
  caller's own mappings before a byte is copied. `REPORT` needs the console
  capability (WRITE) and prints `report <thread> <key> <value>`.
- Edits outside new files: `mod pong;` plus two `#[cfg(feature = "pong")]`
  calls in `main.rs` (boot, finish), one match arm in `thread.rs`, a feature in
  `Cargo.toml`, and a `CARGO_FEATURE_PONG` branch in `build.rs` that runs
  `guest/pong/build.sh`.

## Checks (`ALL HELD: 15 checks` on the cloud container, QEMU 8.2 TCG)

Thread deaths as expected (4), no hostile pixel ever reached the screen,
Pong presented frames and completed all phases, no paced frame missed its
33 ms slot, the VGA memory's hash equals the hash Pong reports for its last
frame, every hostile attempt refused with the right error, 20 000 refused
presents slipped nothing through, the writer died before it could report a
value it read, the forger could not mint or name a display capability, kernel
heap growth within 4 KiB (measured 0), capability table unchanged.

## Measured (cloud container, QEMU TCG, no KVM; NOT the user's PC)

| | |
|---|---|
| Phase A: gameplay paced to 30 fps, 3 s | 91 frames = 29.99 fps, 0 missed slots, 6 tiles per frame, worst frame 2.2 ms |
| Phase B: gameplay uncapped, 2 s | about 58 000 frames (29 000 fps): a frame costs about 34 us when only 2 tiles change |
| Phase C: full-screen redraw uncapped, 2 s | 1 050 to 1 235 fps with all 300 tiles through the syscall each frame, worst frame 4.6 to 6.9 ms |
| Input latency | median 2.7 ms, max 32 ms (QEMU monitor `sendkey` to the game's serial report, host clock; includes QEMU and serial delay; the first two keys of a run land near the 33 ms frame period) |
| Memory | Pong's address space 440 frames = 1 760 KiB (1.6 MiB of it the fixed data/heap window); whole machine 503 frames = 2 012 KiB; kernel heap 3 552 bytes at start, +0 during the run |
| Isolation overhead | a present syscall is one `int 0x80` round trip plus the row copy; phase C's 300 tiles per frame cost under 1 ms per frame under TCG |

`tests/kernel/pong.sh` prints these lines from the serial log and writes
`target/pong-screen.ppm`, a screendump of the emulated VGA during the game.

## Not verified / not done

- Not run on the user's PC or under KVM; the numbers above are TCG on a
  shared cloud box.
- The RCS command validator and the asset store are not in this path yet:
  the game draws into its own canvas and presents tiles; the render service
  as a separate guest app needs shared frames between address spaces (Phase 3
  untyped/frame work).
- The display is QEMU's Bochs VGA; real hardware needs a GOP/VBE framebuffer
  from the bootloader.
- The keyboard is polled once per frame; a key held shorter than a frame can
  be missed in the uncapped phases (not in the paced one).
