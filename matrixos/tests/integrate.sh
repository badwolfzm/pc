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
#   assets: lines 322-408 (v1 mx_vfs, string check + fopen) wrapped in #ifndef MX_VFS_COMPAT,
#           preceded by #include "mx_vfs_compat.h" (openat2 RESOLVE_BENEATH|NO_SYMLINKS)
#   superops: lines 251-320 (enum + unbounded mx_superop_dispatch) wrapped in #ifndef MX_SUPEROPS_COMPAT_H,
#           preceded by #include "mx_superops_compat.h"; mx_superops.c is unity-included at the end;
#           the baseline's two test call sites and the fuzzer call mx_superop_dispatch2 with capacities.
#   fuzz  : lines 480-482 (mx_fuzz_stats, mx_rand_fill, mx_fuzz_run) wrapped in #ifndef MX_FUZZ_COMPAT,
#           preceded by #include "mx_fuzz_compat.h" (tests/fuzz, harness-owned, finding 5).
cp "$ROOT/assets/mx_vfs_compat.h" "$OUT/"
cp "$ROOT/superops/mx_superops.h" "$ROOT/superops/mx_superops.c" "$ROOT/superops/mx_superops_kernels.inc" "$ROOT/superops/mx_superops_compat.h" "$OUT/" 2>/dev/null || true
[ -f "$ROOT/superops/mx_superops_lower.h" ] && cp "$ROOT/superops/mx_superops_lower.h" "$OUT/"
cp "$ROOT/tests/fuzz/mx_fuzz_compat.h" "$OUT/"
{ sed -n '1,250p' "$OUT/baseline.c"
  echo '#include "mx_superops_compat.h"'
  echo '/* keep the baseline enum (the tests name its values); replace the dispatcher */'
  sed -n '251,286p' "$OUT/baseline.c"
  echo '#ifndef MX_SUPEROPS_COMPAT_H'
  sed -n '287,320p' "$OUT/baseline.c"
  echo '#endif /* MX_SUPEROPS_COMPAT_H */'
  cat <<'ADAPT'
#ifdef MX_SUPEROPS_COMPAT_H
/* HARNESS ADAPTER: the baseline dispatcher checked MX_CAP_SUPEROP, emitted a trace event, then switched.
 * Keep that order so the deterministic trace hash is unchanged, then call the bounded dispatcher. */
static int mx_superop_dispatch_b(mx_oracle *o, int id, void *a0, size_t a0_cap, size_t a1, void *a2, size_t a2_cap, size_t a3, uint64_t *ret) {
    if (!o || !(o->caps & MX_CAP_SUPEROP)) return -100;
    mx_trace_event(&o->trace, 0x5355504FULL, (uint64_t)id, (uint64_t)a1);
    return mx_superop_dispatch2(o->caps, id, a0, a0_cap, a1, a2, a2_cap, a3, ret);
}
#endif
ADAPT
  echo '#include "mx_vfs_compat.h"'
  echo '#ifndef MX_VFS_COMPAT'
  sed -n '322,408p' "$OUT/baseline.c"
  echo '#endif /* MX_VFS_COMPAT */'
  sed -n '409,434p' "$OUT/baseline.c"
  echo '#include "mx_lift_compat.h"'
  sed -n '450,479p' "$OUT/baseline.c"
  echo '#include "mx_fuzz_compat.h"'
  echo '#ifndef MX_FUZZ_COMPAT'
  sed -n '480,482p' "$OUT/baseline.c"
  echo '#endif /* MX_FUZZ_COMPAT */'
  sed -n '483,$p' "$OUT/baseline.c"
  echo '#include "mx_superops.c"'; } > "$OUT/integrated.c"
# HARNESS ADAPTATION of the two baseline test call sites: the bounded dispatcher needs capacities
python3 -I - "$OUT/integrated.c" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
a='mx_superop_dispatch(&o,MX_SUPEROP_CRC32,payload,sizeof(payload),NULL,0,&crc)==0'
b='mx_superop_dispatch_b(&o,MX_SUPEROP_CRC32,payload,sizeof(payload),sizeof(payload),NULL,0,0,&crc)==0'
c='mx_superop_dispatch(&o2,MX_SUPEROP_CRC32,payload,sizeof(payload),NULL,0,&ret)!=0'
d='mx_superop_dispatch_b(&o2,MX_SUPEROP_CRC32,payload,sizeof(payload),sizeof(payload),NULL,0,0,&ret)!=0'
assert a in s and c in s, "baseline test call sites not found"
open(p,'w').write(s.replace(a,b).replace(c,d))
PY
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
echo "GOLDEN_TRACE=0x3b3805a77980eee1  # the replacement fuzzer uses its own oracle, so the baseline fuzzer's"
echo "                                 # VFS-init and ~1000 super-op trace events no longer reach the main trace."
echo "                                 # Verified: vfs+lfsr alone keep 0x3e978b49855e4339; fuzz alone (superops swap off) gives 0x3b3805a77980eee1."
echo "run: bash golden_check.sh $OUT/integrated.c <swrap.c> \"gcc -D_GNU_SOURCE -I$OUT\"   (the VFS compat header needs O_PATH)"
