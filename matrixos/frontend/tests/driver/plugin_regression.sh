#!/usr/bin/env bash
# Regression for the "--" passthrough bypass (independent audit 2026-11-05, issue 1).
# A harmless shared object with a constructor writes a marker file when loaded.
# Control: the compiler run directly with -fplugin= loads it (marker written).
# Requirement: every mxcc path that could carry the flag exits 2 and no marker appears.
# Usage: plugin_regression.sh <mxcc binary>
set -u
MXCC="$1"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
fails=0; total=0
ok(){ total=$((total+1)); echo "PASS $1"; }
bad(){ total=$((total+1)); fails=$((fails+1)); echo "FAIL $1"; [ $# -gt 1 ] && echo "     $2"; }

GCC="$(command -v gcc)"; CLANG="$(command -v clang)"
[ -n "$GCC" ] || { echo "SKIP plugin regression: gcc not installed"; exit 0; }
cat > plugin.c <<C
#include <stdio.h>
__attribute__((constructor)) static void mark(void){ FILE *f = fopen("MARKER", "w"); if (f) { fputs("plugin constructor ran\n", f); fclose(f); } }
int plugin_is_GPL_compatible;
C
"$GCC" -shared -fPIC -o plugin.so plugin.c || { echo "SKIP: cannot build shared object"; exit 0; }
printf 'int main(void){return 0;}\n' > t.c

# control: prove the probe works when the compiler is driven directly
rm -f MARKER; "$GCC" -fplugin="$PWD/plugin.so" -c t.c -o t.o >/dev/null 2>&1
if [ -f MARKER ]; then ok "control: gcc -fplugin loads the probe (marker written)"; else echo "SKIP control: gcc did not run the constructor (marker absent); refusal checks still run"; fi

for cc in "$GCC" ${CLANG:+"$CLANG"}; do
  for argv in \
    "-- -fplugin=$PWD/plugin.so" \
    "-fplugin=$PWD/plugin.so" \
    "-- -Xclang -load -Xclang $PWD/plugin.so" \
    "-- -fpass-plugin=$PWD/plugin.so" \
    "-- -Wl,-plugin,$PWD/plugin.so" \
    "-- -Xlinker -plugin -Xlinker $PWD/plugin.so" \
    "-Xlinker -plugin" \
    "-- -specs=$PWD/evil.specs" \
    "-- @$PWD/resp" \
    "-- -B$PWD" \
    "-- -fuse-ld=$PWD/ld" ; do
    rm -f MARKER t.o
    # shellcheck disable=SC2086
    "$MXCC" --cc="$cc" -c t.c -o t.o $argv >out.log 2>&1; rc=$?
    if [ $rc -eq 2 ] && [ ! -f MARKER ] && [ ! -f t.o ]; then ok "[$(basename "$cc")] refused: $argv"
    else bad "[$(basename "$cc")] NOT refused: $argv" "rc=$rc marker=$([ -f MARKER ] && echo yes || echo no) $(head -1 out.log)"; fi
  done
  # benign passthrough after -- still works
  rm -f t.o; "$MXCC" --cc="$cc" -c t.c -o t.o -- -DBENIGN=1 -I "$PWD" >out.log 2>&1 && [ -f t.o ] && ok "[$(basename "$cc")] benign -- passthrough compiles" || bad "[$(basename "$cc")] benign passthrough" "$(head -2 out.log)"
done
echo "plugin_regression: $total checks, $fails failures"
exit $fails
