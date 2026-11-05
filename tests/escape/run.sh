#!/usr/bin/env bash
# Phase 0 escape suite: build the hostile workload, approve only the
# libraries it needs (not libz), and run it under the supervisor.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="$ROOT/target/debug/warden"
WORK="${WORK:-$(mktemp -d)}"
S="$WORK/state"; BIN="$WORK/bin"; WDIR="$WORK/writable"; OUT="$WORK/outside"
rm -rf "$S" "$BIN" "$WDIR" "$OUT"; mkdir -p "$S" "$BIN" "$WDIR" "$OUT"
gcc -O1 -pthread -o "$BIN/hostile" "$ROOT/tests/escape/hostile.c"
cp /usr/bin/true "$BIN/ok-true"
# Victims outside every grant, for the mutating path races.
printf 'keep me\n' > "$OUT/victim-unlink"; mkdir "$OUT/victim-rmdir"
printf 'keep me\n' > "$OUT/victim-chmod"; chmod 600 "$OUT/victim-chmod"
printf 'keep me\n' > "$OUT/victim-trunc"; printf 'keep me\n' > "$OUT/victim-rename"
printf '#!/bin/sh\nexit 42\n' > "$OUT/escape.sh"; chmod 755 "$OUT/escape.sh"
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
"$W" run --state "$S" -v -- "$BIN/hostile" "$WDIR" "$OK" "$BAD" "$OUT" "$BIN/ok-true" 2> "$WORK/supervisor.err"
RC=$?
set -e
# Did any mutating race reach the directory outside the grants?
outside_check() { # name condition
  if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; RC=1; fi
}
outside_check "outside: victim-unlink still there"      '[ -f "$OUT/victim-unlink" ]'
outside_check "outside: victim-rmdir still there"       '[ -d "$OUT/victim-rmdir" ]'
outside_check "outside: no directory created"           '[ ! -e "$OUT/escaped-dir" ]'
outside_check "outside: no symlink created"             '[ ! -e "$OUT/escaped-sym" ] && [ ! -L "$OUT/escaped-sym" ]'
outside_check "outside: no hardlink created"            '[ ! -e "$OUT/escaped-hard" ]'
outside_check "outside: nothing renamed in"             '[ ! -e "$OUT/escaped-rename" ]'
outside_check "outside: victim-rename still there"      '[ -f "$OUT/victim-rename" ]'
outside_check "outside: victim-chmod mode unchanged"    '[ "$(stat -c %a "$OUT/victim-chmod")" = 600 ]'
outside_check "outside: victim-trunc size unchanged"    '[ "$(stat -c %s "$OUT/victim-trunc")" = 8 ]'
echo "--- supervisor: $(grep -c ' kill ' "$S/audit.log") swapped exec images killed"
echo "--- audit log: $(wc -l < "$S/audit.log") decisions, $(grep -c ' deny ' "$S/audit.log") denials; pending library requests:"
grep -v '^#' "$S/pending.wdn" | cut -f2,4 || true
exit $RC
