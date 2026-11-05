#!/usr/bin/env bash
# Build one SIMD kernel source three times (baseline, avx2, avx512) with the
# flags from mx_tiers.h, and archive the three objects plus an optional
# dispatcher TU.  Deterministic: no -march=native, fixed flags, no timestamps.
#
#   mx_tier_build.sh [--cc clang] [--dispatch dispatch.c] [-- extra cflags] -o libk.a kernel.c
#
# Exit codes: 0 ok, 2 usage, 1 compile failure.
set -euo pipefail
CC=clang; OUT=""; DISPATCH=""; EXTRA=(); SRCS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cc) CC="$2"; shift 2;;
    --cc=*) CC="${1#--cc=}"; shift;;
    --dispatch) DISPATCH="$2"; shift 2;;
    -o) OUT="$2"; shift 2;;
    --) shift; EXTRA=("$@"); break;;
    -*) echo "mx_tier_build: unknown option $1" >&2; exit 2;;
    *) SRCS+=("$1"); shift;;
  esac
done
[[ -n "$OUT" && ${#SRCS[@]} -gt 0 ]] || { echo "usage: mx_tier_build.sh [--cc clang] [--dispatch d.c] -o out.a kernel.c... [-- cflags]" >&2; exit 2; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Identical to MX_TIER_MARCH[] in mx_tiers.h.  Changing one without the other is a bug.
MARCH=("-march=x86-64" "-march=x86-64-v3" "-march=x86-64-v4")
COMMON=(-std=c11 -O2 -mtune=generic -fno-ident -fno-asynchronous-unwind-tables -fno-unwind-tables
        -fno-plt -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fstack-clash-protection -fcf-protection=full
        -ffile-prefix-map="$PWD"=. -frandom-seed=mxtier -I"$HERE")
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
OBJS=()
for src in "${SRCS[@]}"; do
  base="$(basename "${src%.*}")"
  for t in 0 1 2; do
    o="$WORK/${base}.tier$t.o"
    "$CC" "${COMMON[@]}" "${MARCH[$t]}" "-DMX_TIER=$t" "${EXTRA[@]}" -c "$src" -o "$o"
    OBJS+=("$o")
  done
done
if [[ -n "$DISPATCH" ]]; then
  o="$WORK/$(basename "${DISPATCH%.*}").dispatch.o"
  "$CC" "${COMMON[@]}" "${MARCH[0]}" -DMX_TIER=0 "${EXTRA[@]}" -c "$DISPATCH" -o "$o"
  OBJS+=("$o")
fi
rm -f "$OUT"
# D = deterministic archive (zero uids/gids/timestamps)
ar rcsD "$OUT" "${OBJS[@]}"
echo "mx_tier_build: $OUT (${#OBJS[@]} objects: $(printf '%s ' "${SRCS[@]}")x3 tiers${DISPATCH:+ + dispatcher})"
