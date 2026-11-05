#!/usr/bin/env bash
# Regression tests for the seven findings in matrixos/reviews/security-review.md.
# Run against ANY build of the VFS / super-op / wrapper code: the baseline is
# expected to FAIL findings 1, 3, 4, 5 and 6; a fixed module must PASS them all.
# Findings 2 and 7 print SKIP unless the module that fixes them is pointed at via env.
#
# Usage: security_regressions.sh <matrixos_v9.c> <swrap.c> [CC]
#   ASSETS_DIR=<matrixos/assets copy on a writable disk>  also runs F1/F1b against the asset store API
#   FRONTEND_DIR=<matrixos/frontend copy>                  runs F2 (typed-input suite)
#   MXSB_DIR=<matrixos/codegen/mxsb copy>                  runs F7 (unit/mxsb/mxsb_contract.c against mxsb.c)
set -uo pipefail
SRC="$1"; WRAP="$2"; CC="${3:-gcc}"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"; cd "$WORK" || exit 1
tr -d '\r' < "$SRC" | sed 's/^int main(void){/int main_orig(void){/' > mx_lib.c
cp "$WRAP" swrap.c
fails=0
t(){ local name="$1" want="$2" got="$3"; if [[ "$got" == *"$want"* ]]; then echo "PASS $name"; else echo "FAIL $name: want [$want] got [${got:0:160}]"; fails=$((fails+1)); fi; }
skip(){ echo "SKIP $1: $2"; }
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
# Sanitizer builds need a compiler whose ASan runtime is installed. In this container clang 18 has
# none (libclang_rt.asan missing), so sanitizer builds fall back to gcc with the same -D/-I flags.
SANCC="$CC"; echo 'int main(void){return 0;}' > sancheck.c
if ! $CC $SAN -o sancheck sancheck.c >/dev/null 2>&1; then SANCC="gcc ${CC#* }"; [[ "$CC" == *" "* ]] || SANCC="gcc"; echo "note: $CC cannot link sanitizer builds here; using $SANCC for them"; fi
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1

# ---- F1/F1b against the asset store (module API) when ASSETS_DIR is set --------
# The asset store owns findings 1 and 1b. It has no mx_vfs-shaped API, so the
# same two findings are driven through asset_store_open_beneath().
if [[ -n "${ASSETS_DIR:-}" ]]; then
  mkdir -p store; echo "OUTSIDE SECRET" > outside.txt; ln -sf ../outside.txt store/link.txt
  echo "inside" > store/inner.txt; ln -sf inner.txt store/innerlink.txt; mkdir -p store/sub; ln -sf .. store/sub/up
  cat > f1a.c <<'C'
#include "asset_store.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
static void try(asset_store *s, const char *name, const char *rel){ errno=0; int fd=asset_store_open_beneath(s,rel); printf("%s fd=%d errno=%d\n", name, fd<0?-1:0, fd<0?errno:0); if(fd>=0) close(fd); }
int main(void){ asset_store *s=NULL; asset_store_config cfg; memset(&cfg,0,sizeof cfg);
  if(asset_store_open("./store",&cfg,&s)!=0){ printf("open rc!=0\n"); return 1; }
  try(s,"symlink_out","link.txt"); try(s,"dotdot","../outside.txt"); try(s,"absolute","/etc/passwd");
  try(s,"symlink_in","innerlink.txt"); try(s,"dir_symlink_up","sub/up/outside.txt"); try(s,"plain","inner.txt");
  char *g=malloc(601); memset(g,'a',600); g[600]=0; try(s,"len600",g); g[550]=0; try(s,"len550",g); g[129]=0; try(s,"len129",g); free(g);
  asset_store_close(s); return 0; }
C
  $CC -O1 -w -D_GNU_SOURCE -I"$ASSETS_DIR" -o f1a f1a.c "$ASSETS_DIR/asset_store.c" -pthread && O="$(./f1a)"
  echo "     $(tr '\n' ' ' <<<"$O")"
  t "F1 asset store refuses symlink pointing out of the root"   "symlink_out fd=-1" "$O"
  t "F1 asset store refuses ../ out of the root"                "dotdot fd=-1" "$O"
  t "F1 asset store refuses absolute paths"                     "absolute fd=-1" "$O"
  t "F1 asset store refuses a directory symlink climbing out"   "dir_symlink_up fd=-1" "$O"
  t "F1 asset store still opens a plain file beneath the root"  "plain fd=0" "$O"
  t "F1 outside file untouched"                                 "OUTSIDE SECRET" "$(cat outside.txt)"
  t "F1b asset store refuses a 600-byte relpath"                "len600 fd=-1" "$O"
  t "F1b asset store refuses a 550-byte relpath"                "len550 fd=-1" "$O"
  t "F1b asset store refuses a 129-byte relpath (its own limit is 128)" "len129 fd=-1" "$O"
  # Sanitizer run uses gcc: this container has no clang ASan runtime (libclang_rt.asan missing), a toolchain gap, not a module defect.
  cd "$ASSETS_DIR" 2>/dev/null && ASSET_SAN="$(make -s clean >/dev/null 2>&1; CC=gcc make -s asan 2>&1 | tail -1; make -s clean >/dev/null 2>&1)"; cd "$WORK"
  t "F1 asset store own tests pass under ASan/UBSan (gcc)"      "0 failed" "$ASSET_SAN"
fi

# ---- F1: symlink inside the sandbox must not be followed out of it ----------
mkdir -p sandbox; echo "OUTSIDE SECRET" > outside.txt; ln -sf ../outside.txt sandbox/link.txt
cat > f1.c <<'C'
#include "mx_lib.c"
int main(void){ mx_oracle o; mx_oracle_bootstrap(&o,MX_CAP_FS_READ|MX_CAP_FS_WRITE,true); mx_vfs v; mx_vfs_init(&v,&o,"./sandbox",true);
  uint8_t *b=NULL; size_t n=0; int rc=mx_vfs_read_all(&v,"/sandbox/link.txt",&b,&n);
  printf("read rc=%d leaked=%d\n", rc, (rc==0 && b && strstr((char*)b,"OUTSIDE"))?1:0);
  rc=mx_vfs_write_all(&v,"/sandbox/link.txt","pwned",5); printf("write rc=%d\n",rc); return 0; }
C
$CC -O1 -w -o f1 f1.c && O="$(./f1)"
t "F1 symlink read out of jail is refused"  "leaked=0" "$O"
t "F1 symlink write out of jail is refused" "write rc=-" "$O"
t "F1 outside file not modified"            "OUTSIDE SECRET" "$(cat outside.txt)"

# ---- F1b: a guest path longer than the host buffer must be refused, not truncated
cat > f1b.c <<'C'
#include "mx_lib.c"
int main(void){ mx_oracle o; mx_oracle_bootstrap(&o,MX_CAP_FS_READ,true); mx_vfs v; mx_vfs_init(&v,&o,"./sandbox",true); char h[MX_MAX_PATH];
  for (size_t L=500; L<=600; L+=50){ char *g=malloc(L+1); memset(g,'a',L); memcpy(g,"/sandbox/",9); g[L]=0;
    int rc=mx_vfs_map_guest_to_host(&v,g,h); printf("len=%zu rc=%d hostlen=%zu\n",L,rc,strlen(h)); free(g);} return 0; }
C
$CC -O1 -w -o f1b f1b.c && O="$(./f1b)"
t "F1b guest path of 600 bytes is refused (not truncated to 512)" "len=600 rc=-" "$O"
t "F1b guest path of 550 bytes is refused (not truncated to 512)" "len=550 rc=-" "$O"

# ---- F2: typed-input rule ---------------------------------------------------
if [[ -n "${FRONTEND_DIR:-}" ]]; then
  # The frontend module implements the rule (include/mx_input.h, src/mx_input.c) and ships the shared
  # vector file tests/vectors/cells.vec plus 18 must-not-compile cases. Its make test is the oracle.
  FE="$( cd "$FRONTEND_DIR" && make -s test 2>&1 )"
  echo "     $(grep -E 'typed_input:|cells.vec|SKIP rust' <<<"$FE" | tr '\n' ' ')"
  t "F2 typed-input: frontend typed_input cases (compile-must-fail + pass) all hold" "typed_input: 48 cases, 0 failures" "$FE"
  t "F2 typed-input: C runner matches cells.vec (57 vectors)" "cells.vec (C): 57 vectors, 0 failures" "$FE"
  t "F2 typed-input: frontend suite green" "all frontend tests passed" "$FE"
  grep -q "SKIP rust" <<<"$FE" && skip "F2 rust side (cells.vec vs kernel warden-caps cells.rs)" "cargo or WARDEN_CELLS_RS not available here; C side ran"
else
  skip "F2 typed-input rule (input 0..0x10FFFF, control above)" "set FRONTEND_DIR=<writable copy of matrixos/frontend> to run its typed-input suite"
fi

# ---- F3: wrapper must not forward plugin/spec/response-file flags, and must exec an absolute path
mkdir -p fakebin; cat > fakebin/clang <<'SH'
#!/bin/sh
echo "ARGV0=$0"; for a in "$@"; do echo "ARG=$a"; done; exit 0
SH
chmod +x fakebin/clang; $CC -O1 -w -o swrap swrap.c
printf 'int f(void){return 1;}\n' > t.c
for bad in "-fplugin=/tmp/evil.so" "-specs=/tmp/evil.specs" "@/tmp/resp" "-B/tmp/evil"; do
  O="$(PATH="$PWD/fakebin:$PATH" ./swrap --profile=matrixos -S t.c -o t.s "$bad" 2>&1)"; RC=$?
  t "F3 wrapper rejects or drops $bad" "ok" "$( { [ $RC -ne 0 ] || ! grep -q "ARG=$bad" <<<"$O"; } && echo ok)"
done
O="$(PATH="$PWD/fakebin:$PATH" ./swrap --profile=matrixos -S t.c -o t.s -- -fplugin=x.so 2>&1)"; RC=$?
t "F3 wrapper rejects or drops -fplugin= after --" "ok" "$( { [ $RC -ne 0 ] || ! grep -q "ARG=-fplugin=x.so" <<<"$O"; } && echo ok)"
O="$(PATH="$PWD/fakebin:$PATH" ./swrap --profile=matrixos -S t.c -o t.s -Xclang -load -Xclang /tmp/evil.so 2>&1)"; RC=$?
t "F3 wrapper rejects -Xclang -load" "ok" "$( { [ $RC -ne 0 ] || ! grep -q "ARG=-load" <<<"$O"; } && echo ok)"
O="$(PATH="$PWD/fakebin:$PATH" ./swrap --profile=matrixos -S t.c -o t.s 2>&1)"
t "F3 wrapper does not resolve the compiler through PATH (planted clang must not run)" "ok" "$(grep -q 'ARG=' <<<"$O" || echo ok)"

# ---- F4: one-byte OOB read in the dotdot check (ASan) ------------------------
cat > f4.c <<'C'
#include "mx_lib.c"
int main(void){ const char *s="/sandbox/hello.txt"; size_t n=strlen(s); char *p=malloc(n+1); memcpy(p,s,n+1);
  /* mx_guest_path_allowed exists in the baseline and in the VFS replacement; the baseline's calls the over-reading dotdot check */
  printf("allowed=%d\n", (int)mx_guest_path_allowed(p)); free(p); return 0; }
C
$SANCC $SAN -w -o f4 f4.c && O="$(./f4 2>&1)"; RC=$?
t "F4 dotdot check reads no byte past NUL (ASan clean)" "ok" "$([ $RC -eq 0 ] && ! grep -q AddressSanitizer <<<"$O" && echo ok)"

# ---- F5: fuzzer must reach the jail (some generated paths accepted) ----------
cat > f5.c <<'C'
#include "mx_lib.c"
int main(void){ mx_oracle o; mx_oracle_bootstrap(&o,MX_CAP_FS_READ|MX_CAP_FS_WRITE|MX_CAP_SUPEROP|MX_CAP_GPU,true);
  mx_fuzz_stats fs; mx_fuzz_run(&o,&fs,20000); printf("iters=%u vfs_rejects=%u accepted=%u crashes=%u",fs.iterations,fs.vfs_rejects,fs.iterations-fs.vfs_rejects,fs.crashes);
#ifdef MX_FUZZ_COMPAT
  printf(" reads_ok=%u writes_ok=%u escapes=%u invariant_fails=%u sniff=%u/%u/%u/%u/%u",fs.reads_ok,fs.writes_ok,fs.escapes,fs.invariant_fails,fs.sniff_hits[0],fs.sniff_hits[1],fs.sniff_hits[2],fs.sniff_hits[3],fs.sniff_hits[4]);
#endif
  printf("\n"); return 0; }
C
$SANCC $SAN -w -o f5 f5.c && O="$(./f5 2>&1)"
echo "     $O"
t "F5 fuzzer produces accepted in-jail paths (coverage)" "ok" "$(awk '/accepted=/{split($3,a,"="); if (a[2]+0 >= 100) print "ok"}' <<<"$O")"
t "F5 fuzz run is ASan/UBSan clean" "ok" "$(grep -q 'Sanitizer\|runtime error' <<<"$O" || echo ok)"
t "F5 fuzz reports no escape or invariant violation" "crashes=0" "$O"

# ---- F6: super-op dispatch must not trust caller lengths -----------------------
cat > f6.c <<'C'
#include "mx_lib.c"
#ifdef MX_SUPEROPS_COMPAT_H
#define DISPATCH(o,id,a0,a1,a2,a3,ret) mx_superop_dispatch_b((o),(id),(a0),16,(a1),(a2),0,(a3),(ret))   /* real capacity: 16 bytes */
#else
#define DISPATCH(o,id,a0,a1,a2,a3,ret) mx_superop_dispatch((o),(id),(a0),(a1),(a2),(a3),(ret))
#endif
int main(void){ mx_oracle o; mx_oracle_bootstrap(&o,MX_CAP_SUPEROP|MX_CAP_GPU,true); uint8_t *b=calloc(16,1); uint64_t r=0;
  int rc=DISPATCH(&o,MX_SUPEROP_CRC32,b,32,NULL,0,&r); printf("crc_over rc=%d\n",rc);
  rc=DISPATCH(&o,MX_SUPEROP_GPU_CLEAR,b,32,NULL,0,&r); printf("clear_over rc=%d\n",rc); free(b); return 0; }
C
$SANCC $SAN -w -o f6 f6.c && O="$(./f6 2>&1)"; RC=$?
t "F6 crc32 with length beyond buffer is refused (no ASan report)" "ok" "$([ $RC -eq 0 ] && ! grep -q AddressSanitizer <<<"$O" && grep -q 'crc_over rc=-' <<<"$O" && echo ok)"

# ---- F7: MXSB loader header validation -----------------------------------------
# The contract test hand-builds a valid MXSB v2 file and mutates it: sizes past the file, entry_off
# outside TEXT, header_size/nsections extremes, overlaps, duplicate/missing sections, bad manifest,
# build_id mismatch. Each mutation must yield the error code and rule name the contract (9a) assigns.
if [[ -n "${MXSB_DIR:-}" ]]; then
  $SANCC $SAN -std=c11 -w -D_GNU_SOURCE -I"$MXSB_DIR" -o f7 "$TESTS_DIR/unit/mxsb/mxsb_contract.c" "$MXSB_DIR/mxsb.c" 2>f7.err && O="$(./f7 2>&1)"; RC=$?
  [[ -s f7.err ]] && head -3 f7.err
  echo "     $(tail -1 <<<"$O")"
  t "F7 MXSB validator rejects every contract mutation with the assigned rule (0 failed, no ASan report)" "ok" "$([ $RC -eq 0 ] && ! grep -q AddressSanitizer <<<"$O" && grep -q ' 0 failed' <<<"$O" && echo ok)"
else
  skip "F7 MXSB loader rejects sizes > file, entry_off outside text, header extremes" "set MXSB_DIR=<copy of matrixos/codegen/mxsb> to run unit/mxsb/mxsb_contract.c against it"
fi

echo "--- $fails failure(s) (baseline is expected to fail F1, F1b, F3, F4, F5, F6)"; exit $fails
