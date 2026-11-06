#!/usr/bin/env bash
# Launch the reviewed Warden web console and an existing QEMU guest together.
# Run from WSL/Linux: bash tools/warden-web-console/run_with_qemu.sh /path/to/kernel.mb32
# This is a host-side QEMU screenshot relay, NOT a Doom native build or boot proof.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KERNEL="${1:-}"
if [[ -z "$KERNEL" || ! -f "$KERNEL" ]]; then
  echo "Usage: bash tools/warden-web-console/run_with_qemu.sh /absolute/path/to/warden-game.mb32" >&2
  exit 2
fi
KERNEL="$(realpath "$KERNEL")"
for bin in node python3 qemu-system-x86_64 openssl; do
  command -v "$bin" >/dev/null || { echo "Missing required command: $bin" >&2; exit 2; }
done
export WARDEN_WEB_HOST=127.0.0.1
export WARDEN_WEB_PORT="${WARDEN_WEB_PORT:-8787}"
export WARDEN_WEB_ORIGIN="http://localhost:${WARDEN_WEB_PORT}"
# Prevent accidentally connecting to an old, differently authenticated server.
python3 - "$WARDEN_WEB_PORT" <<'PY'
import socket,sys
port=int(sys.argv[1])
with socket.socket() as s:
    try:
        s.bind(('127.0.0.1',port))
    except OSError:
        raise SystemExit(f'Port {port} is in use. Stop the existing Warden web server first.')
PY
read -rsp "Web console password (12+ characters): " WARDEN_WEB_PASSWORD
echo
if [[ "${#WARDEN_WEB_PASSWORD}" -lt 12 ]]; then
  echo "Password is too short." >&2
  exit 2
fi
export WARDEN_WEB_PASSWORD
# New shared token lives in these local child environments only, never logs or files.
export WARDEN_INGEST_TOKEN="$(openssl rand -hex 32)"
DIR="$(mktemp -d "${TMPDIR:-/tmp}/warden-web-qmp.XXXXXXXX")"
chmod 700 "$DIR"
QMP_SOCKET="$DIR/qmp.sock"
WEB_PID="" QEMU_PID=""
cleanup() {
  trap - EXIT INT TERM
  [[ -z "$QEMU_PID" ]] || kill "$QEMU_PID" 2>/dev/null || true
  [[ -z "$WEB_PID" ]] || kill "$WEB_PID" 2>/dev/null || true
  [[ -z "$QEMU_PID" ]] || wait "$QEMU_PID" 2>/dev/null || true
  [[ -z "$WEB_PID" ]] || wait "$WEB_PID" 2>/dev/null || true
  rm -rf -- "$DIR"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

node "$ROOT/tools/warden-web-console/server.js" &
WEB_PID=$!
qemu-system-x86_64 -machine pc -smp 1 -m 128M \
  -display none -vga std -serial "file:$DIR/serial.log" \
  -no-reboot -no-shutdown \
  -qmp "unix:$QMP_SOCKET,server=on,wait=off" \
  -kernel "$KERNEL" &
QEMU_PID=$!

for attempt in {1..60}; do
  if ! kill -0 "$QEMU_PID" 2>/dev/null; then
    echo "QEMU exited before creating QMP; check the kernel image." >&2
    cat "$DIR/serial.log" >&2 || true
    exit 1
  fi
  if ! kill -0 "$WEB_PID" 2>/dev/null; then
    echo "Web server exited; port or configuration error." >&2
    exit 1
  fi
  [[ -S "$QMP_SOCKET" ]] && break
  sleep 0.1
done
if [[ ! -S "$QMP_SOCKET" ]]; then
  echo "QMP socket not ready; QEMU output:" >&2
  cat "$DIR/serial.log" >&2 || true
  exit 1
fi
echo "Open $WARDEN_WEB_ORIGIN in your browser."
echo "The guest is the exact kernel image you supplied; this does not install Doom."
echo "QEMU log: $DIR/serial.log (temporary; removed on exit)"
python3 "$ROOT/tools/warden-web-console/qmp_bridge.py" \
  --qmp-socket "$QMP_SOCKET" --fps "${WARDEN_WEB_FPS:-5}"
