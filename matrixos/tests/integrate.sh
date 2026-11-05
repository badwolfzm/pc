#!/usr/bin/env bash
# Build the integrated source: the frozen baseline with module code swapped in.
# Writes <out>/integrated.c and prints the extra CC flags the harness needs.
#   bash integrate.sh <matrixos_v9.c> <out dir>
# Modules wired so far:
#   lfsr  : lines 435-449 (v1 lifter) -> #include "mx_lift_compat.h"; the fuzz
#           loop's detector call is adapted to the v2 signature (harness edit).
set -euo pipefail
SRC="$1"; OUT="$2"; ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$OUT"
tr -d '\r' < "$SRC" > "$OUT/baseline.c"
cp "$ROOT/lfsr/mx_lift.h" "$ROOT/lfsr/mx_lift_compat.h" "$OUT/"
{ sed -n '1,434p' "$OUT/baseline.c"; echo '#include "mx_lift_compat.h"'; sed -n '450,$p' "$OUT/baseline.c"; } > "$OUT/integrated.c"
if grep -q 'mx_loop_sig' "$OUT/mx_lift_compat.h"; then
  echo "compat header provides the legacy mx_loop_sig detector; no fuzz-site adaptation needed"
else
python3 -I - "$OUT/integrated.c" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
old='mx_loop_sig sig={.tag="fuzz", .ops_mask=scratch[3], .has_shift_right1=!!(scratch[4]&1), .has_xor=!!(scratch[5]&1), .has_and_lowbit=!!(scratch[6]&1), .has_const_poly=!!(scratch[7]&1), .has_const_affine=!!(scratch[8]&1)}; mx_loop_kind k=mx_detect_liftable_loop(&sig); if(k==MX_LOOP_LFSR32_AFFINE)'
new='/* HARNESS ADAPTATION: v2 detector takes a loop body */ mx_loop_kind k=mx_detect_liftable_loop(&mx_v1_lfsr_loop,NULL,NULL); if(k==MX_LOOP_GF2_AFFINE)'
assert old in s, "fuzz detector call site not found; baseline changed?"
open(p,'w').write(s.replace(old,new))
PY
fi
echo "integrated: $OUT/integrated.c"
echo "run: bash golden_check.sh $OUT/integrated.c <swrap.c> \"gcc -I$OUT\""
