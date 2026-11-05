#!/usr/bin/env bash
# Phase 0 escape suite: build the hostile workload, approve only the
# libraries it needs (not libz), and run it under the supervisor.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="$ROOT/target/debug/warden"
WORK="${WORK:-$(mktemp -d)}"
S="$WORK/state"; BIN="$WORK/bin"; WDIR="$WORK/writable"
rm -rf "$S" "$BIN" "$WDIR"; mkdir -p "$S" "$BIN" "$WDIR"
gcc -O1 -pthread -o "$BIN/hostile" "$ROOT/tests/escape/hostile.c"
OK=47301; BAD=47302
python3 - "$OK" "$BAD" <<'PY' &
import socket, sys, threading
def serve(port):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", int(port))); s.listen(512)
    while True:
        c, _ = s.accept(); c.close()
for p in sys.argv[1:]:
    threading.Thread(target=serve, args=(p,), daemon=True).start()
threading.Event().wait(60)
PY
LISTENERS=$!
trap 'kill $LISTENERS 2>/dev/null || true' EXIT
sleep 0.5
printf 'fs.read\t/usr/lib\nfs.read\t/lib\nfs.read\t/lib64\nfs.write\t%s\nexec\t%s\nexec\t/usr/bin\nnet.connect\t127.0.0.1:%s\nproc.spawn\n' \
  "$WDIR" "$BIN" "$OK" > "$S/manifest.wdn"
"$W" scan --state "$S" "$BIN/hostile" > /dev/null
printf 'all\ndone\n' | "$W" approve --state "$S" > /dev/null
set +e
"$W" run --state "$S" -v -- "$BIN/hostile" "$WDIR" "$OK" "$BAD" 2> "$WORK/supervisor.err"
RC=$?
set -e
echo "--- audit log: $(wc -l < "$S/audit.log") decisions, $(grep -c ' deny ' "$S/audit.log") denials; pending library requests:"
grep -v '^#' "$S/pending.wdn" | cut -f2,4 || true
exit $RC
