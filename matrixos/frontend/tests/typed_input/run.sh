#!/usr/bin/env bash
# Typed-input compile tests.
#  pass/*.c : must compile, link against mx_input.c and run with exit 0 -- with gcc AND clang
#  fail/*.c : must be refused at compile time -- by gcc AND clang
# Usage: run.sh <mxcc binary> <frontend dir>
set -u
MXCC="$1"; ROOT="$2"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0; total=0
for cc in gcc clang; do
  command -v "$cc" >/dev/null || { echo "SKIP $cc not installed"; continue; }
  for f in "$ROOT"/tests/typed_input/pass/*.c; do
    total=$((total+1)); n="$(basename "$f" .c)"
    if "$MXCC" --cc=$cc --typed-input --strict --mx-include="$ROOT/include" "$f" "$ROOT/src/mx_input.c" -o "$WORK/$n.$cc" >"$WORK/$n.$cc.log" 2>&1 \
       && "$WORK/$n.$cc"; then echo "PASS [$cc] pass/$n"; else echo "FAIL [$cc] pass/$n (rc=$?)"; sed 's/^/     /' "$WORK/$n.$cc.log" | head -20; fails=$((fails+1)); fi
  done
  for f in "$ROOT"/tests/typed_input/fail/*.c; do
    total=$((total+1)); n="$(basename "$f" .c)"
    if "$MXCC" --cc=$cc --typed-input --mx-include="$ROOT/include" -c "$f" -o "$WORK/$n.$cc.o" >"$WORK/$n.$cc.log" 2>&1; then
      echo "FAIL [$cc] fail/$n compiled but must be refused"; fails=$((fails+1))
    else echo "PASS [$cc] fail/$n refused: $(grep -m1 -o 'error:.*' "$WORK/$n.$cc.log" | cut -c1-90)"; fi
  done
done
echo "typed_input: $total cases, $fails failures"
exit $fails
