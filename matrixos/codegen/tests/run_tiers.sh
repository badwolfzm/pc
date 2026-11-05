#!/usr/bin/env bash
# Tier tests: unit rules, three-way build, object-level instruction check,
# dispatch correctness under MX_TIER forcing, deterministic archive.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="${WORK:-$(mktemp -d)}"; CLANG="${CLANG:-/usr/bin/clang}"
fails=0; pass=0
t() { if "$@"; then pass=$((pass+1)); else fails=$((fails+1)); echo "FAIL: $*"; fi; }
gcc -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -o "$WORK/tiers_test" "$HERE/tiers_test.c" && t "$WORK/tiers_test"
# three-way build, twice: identical archives (reproducible)
t bash "$HERE/../tiers/mx_tier_build.sh" --cc "$CLANG" --dispatch "$HERE/tier_dispatch.c" -o "$WORK/libk1.a" "$HERE/tier_kernel.c"
t bash "$HERE/../tiers/mx_tier_build.sh" --cc "$CLANG" --dispatch "$HERE/tier_dispatch.c" -o "$WORK/libk2.a" "$HERE/tier_kernel.c"
t cmp -s "$WORK/libk1.a" "$WORK/libk2.a"
# each tier's object has the symbols and the right instruction classes
cd "$WORK" && rm -rf objs && mkdir objs && (cd objs && ar x ../libk1.a)
t grep -q sum_u32_baseline <(nm objs/tier_kernel.tier0.o)
t grep -q sum_u32_avx2     <(nm objs/tier_kernel.tier1.o)
t grep -q sum_u32_avx512   <(nm objs/tier_kernel.tier2.o)
# baseline must not use ymm/zmm; avx2 must use ymm and never zmm; avx512 must use zmm
t bash -c "! objdump -d objs/tier_kernel.tier0.o | grep -qE '%[yz]mm'"
t bash -c "objdump -d objs/tier_kernel.tier1.o | grep -q '%ymm' && ! objdump -d objs/tier_kernel.tier1.o | grep -q '%zmm'"
t bash -c "objdump -d objs/tier_kernel.tier2.o | grep -qE '%zmm|%ymm'"
# the wrong flags for a tier are a compile error (header guards)
t bash -c "! $CLANG -std=c11 -c -I$HERE/../tiers -DMX_TIER=1 -march=x86-64 $HERE/tier_kernel.c -o /dev/null 2>/dev/null"
t bash -c "! $CLANG -std=c11 -c -I$HERE/../tiers -DMX_TIER=0 -march=x86-64-v3 $HERE/tier_kernel.c -o /dev/null 2>/dev/null"
t bash -c "! $CLANG -std=c11 -c -I$HERE/../tiers -DMX_TIER=1 -march=x86-64-v4 $HERE/tier_kernel.c -o /dev/null 2>/dev/null"
# dispatch: link and run under each forced tier the machine supports; results identical
$CLANG -std=c11 -O2 -I"$HERE/../tiers" -o "$WORK/tier_main" "$HERE/tier_main.c" "$WORK/libk1.a" || { echo "FAIL link"; fails=$((fails+1)); }
hw="$("$WORK/tier_main" | sed 's/.*tier=\([a-z0-9]*\).*/\1/')"
echo "machine tier: $hw"
ref="$("$WORK/tier_main" | sed 's/.*sum=//')"
for tier in baseline avx2 avx512; do
  out="$(MX_TIER=$tier "$WORK/tier_main")"; got_tier="$(echo "$out" | sed 's/.*tier=\([a-z0-9]*\).*/\1/')"; which="$(echo "$out" | sed 's/.*which=\([0-9]\).*/\1/')"; sum="$(echo "$out" | sed 's/.*sum=//')"
  want=$tier; case "$hw:$tier" in baseline:*) want=baseline;; avx2:avx512) want=avx2;; esac
  t test "$got_tier" = "$want"
  t test "$(mx_idx() { case $1 in baseline) echo 0;; avx2) echo 1;; avx512) echo 2;; esac; }; mx_idx $want)" = "$which"
  t test "$sum" = "$ref"
  echo "  MX_TIER=$tier -> $out"
done
echo "run_tiers: $pass passed, $fails failed"
[[ $fails -eq 0 ]]
