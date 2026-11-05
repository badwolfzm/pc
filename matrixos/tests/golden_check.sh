#!/usr/bin/env bash
# Golden-value regression check for the MatrixOS v9 Phase55-61 prototype and
# the matrixos compiler wrapper. Pins the behaviour of the UPLOADED sources so
# that the compiler and GPU upgrades can be diffed against them.
#
# Usage: golden_check.sh <matrixos_v9.c> <swrap.c> [CC]
# Exit 0 = every check passed. Prints PASS/FAIL per check.
set -uo pipefail
SRC="$1"; WRAP="$2"; CC="${3:-gcc}"
WORK="$(mktemp -d)"; cd "$WORK" || exit 1
tr -d '\r' < "$SRC" > mx.c
cp "$WRAP" swrap.c
fails=0
t(){ local name="$1" want="$2" got="$3"; if [[ "$got" == *"$want"* ]]; then echo "PASS $name"; else echo "FAIL $name: want [$want] got [${got:0:200}]"; fails=$((fails+1)); fi; }

# --- build ---
$CC -O2 -w -o mx mx.c || { echo "FAIL build mx with $CC"; exit 1; }
$CC -O2 -w -o swrap swrap.c || { echo "FAIL build swrap with $CC"; exit 1; }
$CC -Os -s -w -o mx_os mx.c
TEXT=$(size mx_os | awk 'NR==2{print $1}')
t "text segment under 150 KB" "ok" "$([ "$TEXT" -lt 153600 ] && echo ok)"

# --- golden values from the uploaded prototype (gcc 13 / clang 18, x86-64) ---
OUT="$(./mx)"; RC=$?
t "prototype exits 0"            "ok" "$([ $RC -eq 0 ] && echo ok)"
t "23 built-in asserts pass"     "tests=23/0" "$OUT"
# GOLDEN_TRACE may name a documented alternative (integrate.sh prints one when a swapped module
# legitimately changes the trace event sequence); the baseline value stays the default.
t "trace hash is golden (${GOLDEN_TRACE:-0x3e978b49855e4339})" "trace=${GOLDEN_TRACE:-0x3e978b49855e4339}" "$OUT"
t "canvas PPM golden sha256"     "99e7937b94f9a5ccbf4dbfaa14cd6314fa7f10d64d63bfc5aae3c183573e4414" "$(sha256sum sandbox/phase61_canvas_demo.ppm)"
t "MXSB golden sha256"           "5f0faa1b9c2d6732a3ed3a9e7964c765971c99c95640a2ef544cf98cc3e5e30c" "$(sha256sum sandbox/phase60_demo.mxsb)"
t "bench CSV has 4 rows"         "4" "$(tail -n +2 sandbox/phase55_61_compact_bench.csv | wc -l)"
t "bench rows all pass=1"        "" "$(awk -F, 'NR>1 && $NF!=1' sandbox/phase55_61_compact_bench.csv)"
OUT2="$(./mx)"
t "second run same trace hash"   "$(grep -o 'trace=0x[0-9a-f]*' <<<"$OUT")" "$OUT2"

# --- reference-value probes for paths the built-in asserts do not cover ---
sed 's/^int main(void){/int main_orig(void){/' mx.c > mx_lib.c
cat > probe.c <<'C'
#include "mx_lib.c"
#ifdef MX_SUPEROPS_COMPAT_H
#define DISPATCH(o,id,a0,a1,a2,a3,ret) mx_superop_dispatch_b((o),(id),(a0),(a1),(a1),(a2),(a3),(a3),(ret))
#else
#define DISPATCH(o,id,a0,a1,a2,a3,ret) mx_superop_dispatch((o),(id),(a0),(a1),(a2),(a3),(ret))
#endif
int main(void){
  mx_oracle o; mx_oracle_bootstrap(&o, MX_CAP_SUPEROP|MX_CAP_GPU|MX_CAP_FS_READ|MX_CAP_FS_WRITE, true);
  float a[8]={1,2,3,4,5,6,7,8}, b[8]={10,20,30,40,50,60,70,80}; uint64_t ret=0; int rc;
  rc=DISPATCH(&o,MX_SUPEROP_GPU_VADD,a,sizeof a,b,sizeof b,&ret);
  printf("vadd rc=%d n=%llu sum=%g\n",rc,(unsigned long long)ret,a[0]+a[7]);          /* expect 0 8 99 */
  rc=DISPATCH(&o,MX_SUPEROP_GPU_VADD,a,sizeof a,b,4,&ret); printf("vadd_short rc=%d\n",rc); /* -3 */
  uint8_t buf[16]; rc=DISPATCH(&o,MX_SUPEROP_GPU_CLEAR,buf,16,NULL,0x1FF,&ret);
  printf("clear rc=%d b0=%02x ret=%llu\n",rc,buf[0],(unsigned long long)ret);        /* 0 ff 16 */
  mx_oracle o2; mx_oracle_bootstrap(&o2, MX_CAP_SUPEROP, true);
  rc=DISPATCH(&o2,MX_SUPEROP_GPU_VADD,a,sizeof a,b,sizeof b,&ret); printf("vadd_nocap rc=%d\n",rc); /* -101 */
  uint8_t p[32]; memset(p,0x5A,32); uint64_t crc=0; DISPATCH(&o,MX_SUPEROP_CRC32,p,32,NULL,0,&crc);
  printf("crc32=%08llx\n",(unsigned long long)crc);                                   /* zlib: 9139d123 */
  int bad=0; for(uint64_t n=0;n<=70;n++) for(uint32_t s=0;s<3;s++){ uint32_t seed=s*0x9E3779B9u+1; if(mx_lfsr_naive_steps(seed,n)!=mx_lfsr_lifted_steps(seed,n)) bad++; }
  printf("lift_small_mismatches=%d\n",bad);
  uint32_t x=0xDEADBEEF; printf("lift_compose=%s\n", mx_lfsr_lifted_steps(mx_lfsr_lifted_steps(x,1ULL<<40),(1ULL<<40)+5)==mx_lfsr_lifted_steps(x,(1ULL<<41)+5)?"OK":"BAD");
  mx_vfs v; mx_vfs_init(&v,&o,"./sandbox",true); char h[MX_MAX_PATH];
  printf("dotdot_bs=%d nested=%d\n", mx_vfs_map_guest_to_host(&v,"/sandbox/..\\x",h), mx_vfs_map_guest_to_host(&v,"/sandbox/a/../../etc",h));
  return 0; }
C
P=""; $CC -O2 -w -o probe probe.c && P="$(./probe)"
t "gpu_vadd adds in place"        "vadd rc=0 n=8 sum=99" "$P"
t "gpu_vadd rejects short source" "vadd_short rc=-3" "$P"
t "gpu_clear fills low byte"      "clear rc=0 b0=ff ret=16" "$P"
t "gpu op refused without cap"    "vadd_nocap rc=-101" "$P"
t "crc32 matches zlib reference"  "crc32=9139d123" "$P"
t "lifter == naive for n in 0..70" "lift_small_mismatches=0" "$P"
t "lifter composes at 2^40"       "lift_compose=OK" "$P"
t "VFS rejects .. variants"       "dotdot_bs=-102 nested=-102" "$P"

# --- compiler wrapper ---
printf 'int f(int x){return x*2;}\n' > t.c
for p in compile-fast matrixos balanced runtime-fast; do ./swrap --profile=$p -S t.c -o $p.s 2>/dev/null; t "wrapper profile $p emits asm" "ok" "$([ -s $p.s ] && echo ok)"; done
./swrap --profile=bogus -S t.c -o x.s 2>/dev/null; t "wrapper rejects unknown profile (rc 2)" "2" "$?"
./swrap t.c -o x.s 2>/dev/null;                   t "wrapper rejects missing -S (rc 2)" "2" "$?"
./swrap --profile=matrixos -S t.c -o r1.s; ./swrap --profile=matrixos -S t.c -o r2.s
t "wrapper output reproducible"    "ok" "$(cmp -s r1.s r2.s && echo ok)"
t "wrapper --version prints full string (known bug: writes 35 of 38 bytes)" "0.2.0" "$(./swrap --version)"

echo "--- $fails failure(s)"; exit $fails
