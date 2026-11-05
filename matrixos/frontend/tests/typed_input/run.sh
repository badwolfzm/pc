#!/usr/bin/env bash
# Typed-input compile tests.
#  pass/*.c : must compile, link against mx_input.c and run with exit 0 -- with gcc AND clang
#  fail/*.c : must be refused by the COMPILER (a real diagnostic) -- by gcc AND clang
# A compiler that is missing, or that mxcc cannot invoke, is a setup error (exit 3),
# never a passing rejection (independent audit 2026-11-05, harness weakness).
# Usage: run.sh <mxcc binary> <frontend dir>
set -u
MXCC="$1"; ROOT="$2"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0; total=0; compilers=0
printf 'int main(void){return 0;}\n' > "$WORK/probe.c"
for name in gcc clang; do
  cc="$(command -v "$name" || true)"
  [ -n "$cc" ] || { echo "SETUP: $name not installed"; continue; }
  # the compiler must be reachable THROUGH mxcc before any negative test counts
  if ! "$MXCC" --cc="$cc" -c "$WORK/probe.c" -o "$WORK/probe.$name.o" >"$WORK/probe.$name.log" 2>&1; then
    echo "SETUP ERROR: mxcc cannot invoke $cc:"; sed 's/^/     /' "$WORK/probe.$name.log"; exit 3
  fi
  compilers=$((compilers+1))
  for f in "$ROOT"/tests/typed_input/pass/*.c; do
    total=$((total+1)); n="$(basename "$f" .c)"
    if "$MXCC" --cc="$cc" --typed-input --strict --mx-include="$ROOT/include" "$f" "$ROOT/src/mx_input.c" -o "$WORK/$n.$name" >"$WORK/$n.$name.log" 2>&1 \
       && "$WORK/$n.$name"; then echo "PASS [$name] pass/$n"; else echo "FAIL [$name] pass/$n (rc=$?)"; sed 's/^/     /' "$WORK/$n.$name.log" | head -20; fails=$((fails+1)); fi
  done
  for f in "$ROOT"/tests/typed_input/fail/*.c; do
    total=$((total+1)); n="$(basename "$f" .c)"
    "$MXCC" --cc="$cc" --typed-input --mx-include="$ROOT/include" -c "$f" -o "$WORK/$n.$name.o" >"$WORK/$n.$name.log" 2>&1; rc=$?
    if [ $rc -eq 0 ]; then echo "FAIL [$name] fail/$n compiled but must be refused"; fails=$((fails+1))
    elif grep -q '^mxcc: error' "$WORK/$n.$name.log" || ! grep -q 'error:' "$WORK/$n.$name.log"; then
      echo "FAIL [$name] fail/$n: no compiler diagnostic (driver or setup failure, not a type refusal)"; sed 's/^/     /' "$WORK/$n.$name.log" | head -5; fails=$((fails+1))
    else echo "PASS [$name] fail/$n refused: $(grep -m1 -o 'error:.*' "$WORK/$n.$name.log" | cut -c1-90)"; fi
  done
done
[ $compilers -gt 0 ] || { echo "SETUP ERROR: neither gcc nor clang available"; exit 3; }
echo "typed_input: $total cases, $fails failures ($compilers compilers)"
exit $fails
