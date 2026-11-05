#!/usr/bin/env bash
# Build the kernel with the Pong guest and run it under QEMU: Pong draws on the
# emulated VGA, three hostile threads try to interfere, keys are injected through
# the QEMU monitor so input latency can be measured, and the kernel's own checks
# decide the exit status. Pass DISPLAY=gtk (or sdl) to watch it; default headless.
#   tests/kernel/pong.sh            headless, 10 s, exit 0 when ALL HELD
#   SHOW=1 tests/kernel/pong.sh     opens the QEMU window (needs a display)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
PROFILE="${PROFILE:-release}"
cargo build -q -p warden-kernel --target x86_64-unknown-none --profile "$PROFILE" --features pong
DIR="$PROFILE"; [[ "$PROFILE" == dev ]] && DIR=debug
K="target/x86_64-unknown-none/$DIR/warden-kernel"
objcopy -I elf64-x86-64 -O elf32-i386 "$K" "$K.pong.mb32"
ACCEL=tcg; [[ -w /dev/kvm ]] && ACCEL=kvm
LOG="${LOG:-target/pong-serial.log}"; : > "$LOG"
PORT="${MONITOR_PORT:-4555}"
DISP=(-display none); [[ "${SHOW:-0}" == 1 ]] && DISP=(-display "${DISPLAY_BACKEND:-gtk}")
qemu-system-x86_64 -machine pc,accel="$ACCEL" -m 128M "${DISP[@]}" -vga std -serial "file:$LOG" \
    -monitor "tcp:127.0.0.1:$PORT,server,nowait" -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel "$K.pong.mb32" &
QPID=$!
# Inject keys while the game runs and time how long until the game reports each one.
python3 - "$LOG" "$PORT" "$QPID" <<'PY' || true
import socket, sys, time, re, os
log, port, qpid = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
def alive():
    try: os.kill(qpid, 0); return True
    except OSError: return False
def wait_line(pat, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout and alive():
        with open(log, errors="replace") as f:
            for line in f:
                if re.search(pat, line): return line.strip()
        time.sleep(0.01)
    return None
if not wait_line(r"pong: ready", 60): sys.exit(0)
time.sleep(0.5)   # let the TSC calibration finish; keys then land in the 30 fps paced phase
mon = socket.create_connection(("127.0.0.1", port), timeout=5)
time.sleep(0.2); mon.recv(4096)
keys = ["w", "s", "up", "down", "w", "s"]
lat = []
seen = 0
for i, key in enumerate(keys):
    time.sleep(0.9)
    t_send = time.time()
    mon.sendall((f"sendkey {key}\n").encode())
    # the game reports every key press as `report pong 100 <code | tick<<16 | frame<<40>`
    t0 = time.time()
    while time.time() - t0 < 2 and alive():
        with open(log, errors="replace") as f:
            n = sum(1 for l in f if l.startswith("report pong 100 "))
        if n > seen:
            lat.append((key, (time.time() - t_send) * 1000)); seen = n; break
        time.sleep(0.002)
# a screenshot of the emulated VGA while the game is in its paced phase
shot = os.environ.get("SCREENSHOT", "target/pong-screen.ppm")
try:
    mon.sendall((f"screendump {shot}\n").encode()); time.sleep(0.3)
except OSError: pass
if lat:
    ms = sorted(v for _, v in lat)
    print(f"input latency (monitor sendkey -> game reports the key over serial, host clock, includes QEMU and serial delay): "
          f"{len(lat)} of {len(keys)} keys seen; median {ms[len(ms)//2]:.1f} ms, max {ms[-1]:.1f} ms; per key: " +
          ", ".join(f"{k}={v:.0f}" for k, v in lat))
else:
    print("input latency: no key report seen")
PY
set +e
wait $QPID; RC=$?
set -e
[[ -s target/pong-screen.ppm ]] && echo "screenshot: target/pong-screen.ppm"
grep -E "^(pong:|PASS|FAIL|ALL HELD|PONG RUN FAILED|report)" "$LOG" | grep -v "^report pong 100" | sed 's/^/  /'
case $RC in
  1) echo "pong in warden: ALL HELD"; exit 0 ;;
  3) echo "pong in warden: FAILED CHECKS"; exit 1 ;;
  *) echo "pong in warden: kernel did not finish (qemu status $RC)"; exit 2 ;;
esac
