#!/usr/bin/env bash
# Regression tests for the seven findings in matrixos/reviews/security-review.md.
# Run against ANY build of the VFS / super-op / wrapper code: the baseline is
# expected to FAIL findings 1, 3, 4, 5 and 6; a fixed module must PASS them all.
# Findings 2 and 7 have no code to test yet and print SKIP with the reason.
#
# Usage: security_regressions.sh <matrixos_v9.c> <swrap.c> [CC]
set -uo pipefail
SRC="$1"; WRAP="$2"; CC="${3:-gcc}"
WORK="$(mktemp -d)"; cd "$WORK" || exit 1
tr -d '\r' < "$SRC" | sed 's/^int main(void){/int main_orig(void){/' > mx_lib.c
cp "$WRAP" swrap.c
fails=0
t(){ local name="$1" want="$2" got="$3"; if [[ "$got" == *"$want"* ]]; then echo "PASS $name"; else echo "FAIL $name: want [$want] got [${got:0:160}]"; fails=$((fails+1)); fi; }
skip(){ echo "SKIP $1: $2"; }
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1

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
skip "F2 typed-input rule (input 0..0x10FFFF, control above)" "no C implementation exists yet; test lands with the frontend module (reference: warden-caps cells.rs)"

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
O="$(PATH="$PWD/fakebin:$PATH" ./swrap --profile=matrixos -S t.c -o t.s -Xclang -load -Xclang /tmp/evil.so 2>&1)"; RC=$?
t "F3 wrapper rejects -Xclang -load" "ok" "$( { [ $RC -ne 0 ] || ! grep -q "ARG=-load" <<<"$O"; } && echo ok)"
O="$(PATH="$PWD/fakebin:$PATH" ./swrap --profile=matrixos -S t.c -o t.s 2>&1)"
t "F3 wrapper does not resolve the compiler through PATH (planted clang must not run)" "ok" "$(grep -q 'ARG=' <<<"$O" || echo ok)"

# ---- F4: one-byte OOB read in the dotdot check (ASan) ------------------------
cat > f4.c <<'C'
#include "mx_lib.c"
int main(void){ const char *s="/sandbox/hello.txt"; size_t n=strlen(s); char *p=malloc(n+1); memcpy(p,s,n+1);
  printf("dotdot=%d\n", mx_path_has_dotdot_segment(p)); free(p); return 0; }
C
$CC $SAN -w -o f4 f4.c && O="$(./f4 2>&1)"; RC=$?
t "F4 dotdot check reads no byte past NUL (ASan clean)" "ok" "$([ $RC -eq 0 ] && ! grep -q AddressSanitizer <<<"$O" && echo ok)"

# ---- F5: fuzzer must reach the jail (some generated paths accepted) ----------
cat > f5.c <<'C'
#include "mx_lib.c"
int main(void){ mx_oracle o; mx_oracle_bootstrap(&o,MX_CAP_FS_READ|MX_CAP_FS_WRITE|MX_CAP_SUPEROP|MX_CAP_GPU,true);
  mx_fuzz_stats fs; mx_fuzz_run(&o,&fs,20000); printf("iters=%u vfs_rejects=%u accepted=%u\n",fs.iterations,fs.vfs_rejects,fs.iterations-fs.vfs_rejects); return 0; }
C
$CC $SAN -w -o f5 f5.c && O="$(./f5 2>&1)"
echo "     $O"
t "F5 fuzzer produces accepted in-jail paths (coverage)" "ok" "$(awk '/accepted=/{split($3,a,"="); if (a[2]+0 >= 100) print "ok"}' <<<"$O")"
t "F5 fuzz run is ASan/UBSan clean" "ok" "$(grep -q 'Sanitizer\|runtime error' <<<"$O" || echo ok)"

# ---- F6: super-op dispatch must not trust caller lengths -----------------------
cat > f6.c <<'C'
#include "mx_lib.c"
int main(void){ mx_oracle o; mx_oracle_bootstrap(&o,MX_CAP_SUPEROP|MX_CAP_GPU,true); uint8_t *b=calloc(16,1); uint64_t r=0;
  int rc=mx_superop_dispatch(&o,MX_SUPEROP_CRC32,b,32,NULL,0,&r); printf("crc_over rc=%d\n",rc);
  rc=mx_superop_dispatch(&o,MX_SUPEROP_GPU_CLEAR,b,32,NULL,0,&r); printf("clear_over rc=%d\n",rc); free(b); return 0; }
C
$CC $SAN -w -o f6 f6.c && O="$(./f6 2>&1)"; RC=$?
t "F6 crc32 with length beyond buffer is refused (no ASan report)" "ok" "$([ $RC -eq 0 ] && ! grep -q AddressSanitizer <<<"$O" && grep -q 'crc_over rc=-' <<<"$O" && echo ok)"

# ---- F7: MXSB loader header validation -----------------------------------------
skip "F7 MXSB loader rejects text_size near UINT32_MAX, entry_off outside text, sizes > file" "no loader exists yet; cases land with matrixos/contracts/mxsb-manifest.md"

echo "--- $fails failure(s) (baseline is expected to fail F1, F1b, F3, F4, F5, F6)"; exit $fails
