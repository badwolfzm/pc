#!/usr/bin/env bash
# Run the existing PR #68 Doom-like renderer as a *native* Warden ring-3
# program, via the already capability-mediated VGA display ABI.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PROFILE="${PROFILE:-release}"
cargo build -q -p warden-kernel --target x86_64-unknown-none --profile "$PROFILE" --features game_probe
DIR="$PROFILE"; [[ "$PROFILE" == dev ]] && DIR=debug
K="target/x86_64-unknown-none/$DIR/warden-kernel"
objcopy -I elf64-x86-64 -O elf32-i386 "$K" "$K.g0.mb32"
ACCEL=tcg; [[ -w /dev/kvm ]] && ACCEL=kvm
LOG="${LOG:-target/game-probe-serial.log}"
mkdir -p "$(dirname "$LOG")"
set +e
timeout "${TIMEOUT:-120}" qemu-system-x86_64 -machine "pc,accel=$ACCEL" -smp 1 -m 128M -display none -vga std -serial "file:$LOG" \
  -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel "$K.g0.mb32"
RC=$?
set -e
grep -E '^(PASS|FAIL|ALL HELD|GAME PROBE FAILED|game-probe:|report game-probe)' "$LOG" || true
if [[ "$RC" != 1 ]]; then echo "game probe failed: qemu status $RC" >&2; exit 1; fi
grep -Eq '^ALL HELD: [1-9][0-9]* checks, 0 failed$' "$LOG"
grep -q '^PASS game-probe completed 60 ring-3 frames$' "$LOG"
grep -q '^PASS game-probe sent 60 framebuffer presents$' "$LOG"
grep -q '^PASS hostile present attempts were denied$' "$LOG"
grep -q '^PASS game-probe exited normally$' "$LOG"
echo "Warden ring-3 game-probe: verified 60 frames"
