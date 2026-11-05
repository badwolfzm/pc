#!/usr/bin/env bash
# Phase 0 compatibility smoke test: unmodified distro binaries under the
# supervisor. Each line prints PASS/FAIL.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="$ROOT/target/debug/warden"
WORK="${WORK:-$(mktemp -d)}"
S="$WORK/state"; D="$WORK/data"
rm -rf "$S" "$D"; mkdir -p "$S" "$D"
echo "hello from a granted file" > "$D/in.txt"
PY="$(command -v python3)"; PYPREFIX="$(dirname "$(dirname "$PY")")"
printf 'fs.read\t/usr\nfs.read\t/lib\nfs.read\t/lib64\nfs.read\t/etc/alternatives\nfs.read\t%s/lib\nfs.write\t%s\nexec\t/usr/bin\nexec\t%s/bin\nproc.spawn\n' \
  "$PYPREFIX" "$D" "$PYPREFIX" > "$S/manifest.wdn"
for p in sh ls cat grep sort wc sha256sum tar "$PY"; do "$W" scan --state "$S" "$p" > /dev/null; done
printf 'all\ndone\n' | "$W" approve --state "$S" > /dev/null
fails=0
t() { # name expected-substring command...
  local name="$1" want="$2"; shift 2
  local out; out="$(cd "$D" && timeout 30 "$W" run --state "$S" -- "$@" 2>&1)"; local rc=$?
  if [[ $rc -eq 0 && "$out" == *"$want"* ]]; then echo "PASS $name"; else echo "FAIL $name (rc=$rc): ${out:0:300}"; fails=$((fails+1)); fi
}
t "cat a granted file"        "hello from a granted"  cat "$D/in.txt"
t "ls -l a granted dir"       "in.txt"                ls -l "$D"
t "sh pipeline"               "1"                     sh -c "cat $D/in.txt | grep -c hello"
t "sort + wc via sh"          "3"                     sh -c "printf 'b\na\nc\n' | sort | wc -l"
t "write then read back"      "written"               sh -c "echo written > $D/out.txt && cat $D/out.txt"
t "sha256sum"                 "in.txt"                sha256sum "$D/in.txt"
t "tar create + list"         "in.txt"                sh -c "cd $D && tar cf a.tar in.txt && tar tf a.tar"
t "python3 runs"              "4"                     "$PY" -c "print(2+2)"
t "python3 reads granted file" "hello"                "$PY" -c "print(open('$D/in.txt').read())"
t "python3 blocked from /etc"  "PermissionError"      "$PY" -c "
try: open('/etc/passwd').read(); print('LEAK')
except PermissionError as e: print('PermissionError', e)"
t "python3 os.walk in grant"  "in.txt"                "$PY" -c "import os; print([f for _,_,fs in os.walk('$D') for f in fs])"

# Per-library permission: libz asks for net.connect. The network opens up
# only after the approved library is actually loaded, and only if its
# net.connect box was ticked.
PORT=47311
python3 -c "
import socket,threading
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('127.0.0.1',$PORT)); s.listen(64)
def run():
    while True: s.accept()[0].close()
threading.Thread(target=run,daemon=True).start(); threading.Event().wait(60)" &
LP=$!; sleep 0.5
ZEXT="$(python3 -c 'import zlib;print(zlib.__file__)')"
ZLIB="$(ls /usr/lib/x86_64-linux-gnu/libz.so.1.* | head -1)"
printf 'libz.so\tnet.connect=127.0.0.1:%s\n' "$PORT" > "$S/libwants.wdn"
"$W" add-lib --state "$S" "$ZEXT" > /dev/null
"$W" add-lib --state "$S" "$ZLIB" > /dev/null
# Boxes: 1 = zlib module load, 2 = libz load, 3 = libz net.connect
NETTEST="
import socket
def tryc():
    s=socket.socket()
    try: s.connect(('127.0.0.1',$PORT)); return 'connected'
    except PermissionError: return 'refused'
print('before', tryc()); import zlib; print('after', tryc())"
printf '1 2\ndone\n' | "$W" approve --state "$S" > /dev/null
printf 'fs.read\t/usr\nfs.read\t/lib\nfs.read\t/lib64\nfs.read\t/etc/alternatives\nfs.read\t%s/lib\nexec\t%s/bin\nnet.connect\t127.0.0.1:1\n' "$PYPREFIX" "$PYPREFIX" > "$S/manifest.wdn"
t "lib net box unticked: stays closed" "after refused" "$PY" -c "$NETTEST"
"$W" add-lib --state "$S" "$ZLIB" > /dev/null   # ask again for the same hash
printf '1 2\ndone\n' | "$W" approve --state "$S" > /dev/null   # now: load + net.connect
t "lib net box ticked: opens on load"  "before refused
after connected" "$PY" -c "$NETTEST"
kill $LP 2>/dev/null
echo "--- $fails failure(s); $(grep -c ' deny ' "$S/audit.log") denials logged"
exit $fails
