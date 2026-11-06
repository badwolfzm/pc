#!/usr/bin/env bash
# Launch the reviewed Warden web console and an existing QEMU guest together.
# Run from WSL/Linux: bash tools/warden-web-console/run_with_qemu.sh /path/to/kernel.mb32
# This is a host-side QEMU screenshot relay, NOT a Doom native build or boot proof.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KERNEL="${1:-${HOME}/warden/pc-game-test/target/warden-game.mb32}"
if [[ -z "$KERNEL" || ! -f "$KERNEL" ]]; then
  echo "Kernel image missing: $KERNEL. Build it from ~/warden/pc-game-test first, or pass the absolute .mb32 path." >&2
  exit 2
fi
KERNEL="$(realpath "$KERNEL")"
for bin in node python3 qemu-system-x86_64 openssl; do
  command -v "$bin" >/dev/null || { echo "Missing required command: $bin" >&2; exit 2; }
done
export WARDEN_WEB_HOST=127.0.0.1
# Use the explicitly requested port or safely select an unused loopback port.
# Never kill an existing server or reuse its unknown ingest token.
if [[ -n "${WARDEN_WEB_PORT:-}" ]]; then
  export WARDEN_WEB_PORT
  python3 - "$WARDEN_WEB_PORT" <<'PY'
import socket,sys
port=int(sys.argv[1])
with socket.socket() as sock:
    try:
        sock.bind(('127.0.0.1',port))
    except OSError as exc:
        raise SystemExit(f'Port {port} unavailable: {exc}. Unset WARDEN_WEB_PORT to auto-select.')
PY
else
  WARDEN_WEB_PORT="$(python3 - <<'PY'
import socket
for port in range(8787, 8817):
    with socket.socket() as sock:
        try:
            sock.bind(('127.0.0.1',port))
        except OSError:
            continue
        print(port)
        break
else:
    raise SystemExit('No free localhost TCP ports between 8787 and 8816')
PY
)"
  export WARDEN_WEB_PORT
fi
export WARDEN_WEB_ORIGIN="http://localhost:${WARDEN_WEB_PORT}"
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
# Wait until the local HTTP server is actually accepting requests before the
# bridge starts posting frames. Merely having a live Node PID is not sufficient.
READY=0
for attempt in {1..50}; do
  if python3 - "$WARDEN_WEB_PORT" <<'PY'
import http.client,sys
try:
    connection=http.client.HTTPConnection('127.0.0.1',int(sys.argv[1]),timeout=0.5)
    connection.request('GET','/')
    response=connection.getresponse()
    if response.status != 200:
        raise SystemExit(1)
except (OSError,TimeoutError):
    raise SystemExit(1)
PY
  then
    READY=1
    break
  fi
  if ! kill -0 "$WEB_PID" 2>/dev/null; then
    echo "Web server exited before it became ready." >&2
    exit 1
  fi
  sleep 0.1
done
if [[ "$READY" != 1 ]]; then
  echo "Web server did not become ready; exiting instead of failing frame uploads." >&2
  exit 1
fi
echo "Open $WARDEN_WEB_ORIGIN in your browser (NOT necessarily port 8787)."
echo "The guest is the exact kernel image you supplied; this does not install Doom."
echo "QEMU log: $DIR/serial.log (temporary; removed on exit)"
python3 "$ROOT/tools/warden-web-console/qmp_bridge.py" \
  --qmp-socket "$QMP_SOCKET" --port "$WARDEN_WEB_PORT" --fps "${WARDEN_WEB_FPS:-5}"
