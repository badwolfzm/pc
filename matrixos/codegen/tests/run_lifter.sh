#!/usr/bin/env bash
# Differential test for the lifter hook: every case is compiled twice with
# plain clang -O2, once from the original source and once from the hooked
# source, and both binaries must print identical output.  Cases named c*
# must have at least one lifted loop and their matrix in .rodata; cases
# named n* must have none.  Also checks the hook's own sanitizer build.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LFSR="${LFSR_DIR:-$HERE/../../lfsr}"
CLANG="${CLANG:-/usr/bin/clang}"
WORK="${WORK:-$(mktemp -d)}"
HOOK="$WORK/mxlift_hook"
fails=0; pass=0
say() { echo "$@"; }
gcc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$LFSR" -o "$HOOK" "$HERE/../lifter/mxlift_hook.c" || { echo "FAIL build hook"; exit 1; }
export ASAN_OPTIONS=detect_leaks=0
for src in "$HERE"/lifter_cases/*.c; do
  name="$(basename "${src%.c}")"
  "$CLANG" -std=c11 -O2 -w -I"$HERE/lifter_cases" -o "$WORK/$name.orig" "$src" || { echo "FAIL $name: original does not compile"; fails=$((fails+1)); continue; }
  log="$($HOOK "$src" -o "$WORK/$name.hooked.c" --report -- -I"$HERE/lifter_cases" 2>&1)"; rc=$?
  if [[ $rc -ne 0 ]]; then echo "FAIL $name: hook rc=$rc"; echo "$log"; fails=$((fails+1)); continue; fi
  nlift="$(echo "$log" | grep -c ': lifted loop')"
  "$CLANG" -std=c11 -O2 -w -I"$HERE/lifter_cases" -I"$LFSR" -o "$WORK/$name.hooked" "$WORK/$name.hooked.c" || { echo "FAIL $name: hooked source does not compile"; echo "$log"; fails=$((fails+1)); continue; }
  "$WORK/$name.orig" > "$WORK/$name.a" 2>&1; ra=$?
  "$WORK/$name.hooked" > "$WORK/$name.b" 2>&1; rb=$?
  if [[ $ra -ne $rb ]] || ! cmp -s "$WORK/$name.a" "$WORK/$name.b"; then echo "FAIL $name: outputs differ (rc $ra vs $rb)"; diff "$WORK/$name.a" "$WORK/$name.b" | head -5; fails=$((fails+1)); continue; fi
  case "$name" in
    c*) if [[ "$nlift" -lt 1 ]]; then echo "FAIL $name: expected a lifted loop"; echo "$log" | grep -v 'does not start'; fails=$((fails+1)); continue; fi
        # the matrix must live in .rodata (nm type 'r')
        if ! nm "$WORK/$name.hooked" | grep -q ' r __mxl_aff_0'; then echo "FAIL $name: __mxl_aff_0 not in .rodata"; nm "$WORK/$name.hooked" | grep mxl_aff; fails=$((fails+1)); continue; fi ;;
    n*) if [[ "$nlift" -ne 0 ]]; then echo "FAIL $name: must not be lifted"; echo "$log"; fails=$((fails+1)); continue; fi ;;
  esac
  lines=$(wc -l < "$WORK/$name.a")
  echo "PASS $name: $nlift lifted, $lines output lines identical"
  pass=$((pass+1))
done
# flag allowlist: dangerous flags must be refused (security review finding 3)
for bad in "-fplugin=/x/evil.so" "-Xclang" "@resp" "-specs=/x" "-B/x" "-fpass-plugin=/x.so"; do
  if $HOOK "$HERE/lifter_cases/c01_galois32.c" --dry-run -- "$bad" >/dev/null 2>&1; then echo "FAIL flag $bad was accepted"; fails=$((fails+1)); else pass=$((pass+1)); fi
done
echo "run_lifter: $pass passed, $fails failed (work dir $WORK)"
[[ $fails -eq 0 ]]
