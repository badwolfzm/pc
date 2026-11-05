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
# Independently compare the ring-3 rolling pixel digest with the unchanged
# host reference from PR #68, built from the same source with a host compiler.
# This catches a consistent but wrong renderer/ABI/guest-memory result that
# positive frame counts alone would miss.
cc -std=c11 -O2 -Wall -Wextra -Werror \
  tools/game-frame-probe/render.c tools/game-frame-probe/main.c \
  -o target/game-probe-host-reference
target/game-probe-host-reference 60 > target/game-probe-host-reference.txt
python3 - "$LOG" target/game-probe-host-reference.txt <<'PY'
import pathlib, re, sys
guest = pathlib.Path(sys.argv[1]).read_text(errors="replace")
host = pathlib.Path(sys.argv[2]).read_text()
gm = re.search(r"^report game-probe 41 ([0-9]+)$", guest, re.MULTILINE)
hm = re.search(r"\bdigest=([0-9a-fA-F]{16})\b", host)
if gm is None or hm is None:
    raise SystemExit("missing guest or host digest: FAIL")
got, expected = int(gm.group(1)), int(hm.group(1), 16)
print(f"Warden G0: guest digest {got:016x}, host reference {expected:016x}")
if got != expected:
    raise SystemExit("guest and host renderer digests disagree")
PY
echo "Warden ring-3 game-probe: verified 60 frames"
