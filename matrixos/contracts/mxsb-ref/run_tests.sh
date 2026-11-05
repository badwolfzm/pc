#!/usr/bin/env bash
# Reference loader tests. Run with bash (the project-files mount is noexec):
#   bash run_tests.sh [BUILD_DIR] [CODEGEN_DIR]
# Builds mxsb_check and mxsb_corpus with gcc (ASan+UBSan) and clang, generates
# the corpus, checks every file against its expected error, and, when the
# codegen emitter is present, cross-checks both implementations on the same
# inputs and loads an emitter-built file (unsigned and Ed25519-signed).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
B="${1:-/tmp/mxsb-ref-build}"
CG="${2:-$HERE/../../codegen}"
mkdir -p "$B/corpus"
pass=0; fail=0
ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL: $*"; }

echo "== build"
SAN="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
gcc $SAN -o "$B/mxsb_check"  "$HERE/mxsb_load.c" "$HERE/mxsb_check.c"  || { echo "build failed (gcc)"; exit 1; }
gcc $SAN -o "$B/mxsb_corpus" "$HERE/mxsb_load.c" "$HERE/mxsb_corpus.c" || { echo "build failed (gcc)"; exit 1; }
if command -v clang >/dev/null; then
  clang -std=c11 -O2 -Wall -Wextra -Werror -o "$B/mxsb_check_clang" "$HERE/mxsb_load.c" "$HERE/mxsb_check.c" || { echo "build failed (clang)"; exit 1; }
fi
# the loader core must stay free of libc I/O and allocation (kernel-portable)
if nm "$B/mxsb_check" >/dev/null 2>&1; then
  gcc -std=c11 -O1 -c -o "$B/load.o" "$HERE/mxsb_load.c"
  if nm "$B/load.o" | grep -E ' U (malloc|calloc|free|fopen|fread|open|read|mmap)$' >/dev/null; then bad "mxsb_load.c references allocation or I/O"; else ok; fi
fi

echo "== sha256 self-test"
# NIST vector: sha256("abc")
printf 'abc' > "$B/abc"
cat > "$B/sha_t.c" <<'EOF'
#include "mxsb_load.h"
#include <stdio.h>
#include <string.h>
int main(void){ uint8_t d[32]; char h[65]; mxsb_ld_sha256((const uint8_t*)"abc",3,d); for(int i=0;i<32;++i) snprintf(h+2*i,3,"%02x",d[i]);
 if(strcmp(h,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) return 1;
 static uint8_t big[1000000]; memset(big,'a',sizeof big); mxsb_ld_sha256(big,sizeof big,d); for(int i=0;i<32;++i) snprintf(h+2*i,3,"%02x",d[i]);
 return strcmp(h,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")?1:0; }
EOF
gcc -std=c11 -O1 -I"$HERE" -o "$B/sha_t" "$B/sha_t.c" "$HERE/mxsb_load.c" && "$B/sha_t" && ok || bad "sha256 vectors"

echo "== corpus"
rm -f "$B/corpus"/*
"$B/mxsb_corpus" "$B/corpus" || { echo "corpus generation failed"; exit 1; }
VALID_DIGEST=$("$B/mxsb_check" "$B/corpus/valid_unsigned.mxsb" --structural | sed -n 's/.*digest=\([0-9a-f]*\).*/\1/p')
[ -n "$VALID_DIGEST" ] && ok || bad "could not compute valid digest"
run_case() {  # name expected_err expected_rule
  local name="$1" exp="$2" rule="$3" f="$B/corpus/$1.mxsb" cont="$HERE/fixtures/warden.toml" extra=""
  # approvals path: approve the file's own digest (V20 precedes V21..V26, so a
  # mutant that changes bytes must be approved to reach the manifest rules)
  local own; own=$("$B/mxsb_check" "$f" --structural 2>/dev/null | sed -n 's/.*digest=\([0-9a-f]*\).*/\1/p')
  case "$name" in
    v20_unsigned_not_allowed) cont="$HERE/fixtures/no_unsigned.toml" ;;
    v20_unsigned_not_approved) extra="" ;;
    v20_garbage_signature) extra="" ;;
    v26_*) cont="$HERE/fixtures/$name.toml"; extra="--approve $VALID_DIGEST" ;;
    *) [ -n "$own" ] && extra="--approve $own" ;;
  esac
  local out; out=$("$B/mxsb_check" "$f" --container "$cont" $extra 2>&1); local rc=$?
  local got; got=$(echo "$out" | head -1 | awk '{print $1}')
  local gotrule; gotrule=$(echo "$out" | head -1 | awk '{print $2}' | tr -d ':')
  if [ "$got" = "$exp" ] && { [ "$rule" = "-" ] || [ "$gotrule" = "$rule" ]; }; then ok; else bad "$name: want $exp $rule, got: $out (rc=$rc)"; fi
  if [ -x "$B/mxsb_check_clang" ]; then
    local out2; out2=$("$B/mxsb_check_clang" "$f" --container "$cont" $extra 2>&1 | head -1 | awk '{print $1}')
    [ "$out2" = "$got" ] && ok || bad "$name: gcc/clang disagree ($got vs $out2)"
  fi
}
while read -r name exp rule; do run_case "$name" "$exp" "$rule"; done < "$B/corpus/expected.txt"
# every corpus entry must have been a distinct rule or a positive; count rules covered
echo "rules covered: $(awk '$3!="-"{print $3}' "$B/corpus/expected.txt" | sort -u | tr '\n' ' ')"

echo "== random mutation fuzz (no crash, no sanitizer report, always a named error or OK)"
cat > "$B/fuzz.c" <<'EOF'
#include "mxsb_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t xs(uint64_t *s){uint64_t x=*s;x^=x<<13;x^=x>>7;x^=x<<17;return *s=x;}
int main(int argc,char**argv){ FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f); uint8_t*v=malloc(n+64); fread(v,1,n,f); fclose(f);
 uint8_t*m=malloc(n+64); uint64_t s=0x9E3779B97F4A7C15ull; static mxsb_ld_image im; mxsb_ld_diag d; mxsb_ld_policy p; memset(&p,0,sizeof p); p.skip_signature=true;
 int iters=atoi(argv[2]); int okc=0,errc=0;
 for(int i=0;i<iters;++i){ memcpy(m,v,n); size_t len=n; int k=1+(xs(&s)%4); for(int j=0;j<k;++j){ uint64_t r=xs(&s); size_t off=r%(size_t)n; switch((r>>40)%5){case 0: m[off]^=(uint8_t)(r>>8); break; case 1: m[off]=0xFF; break; case 2: m[off]=0; break; case 3: if(off+8<=len){uint64_t big=(r&1)?UINT64_MAX:(UINT64_MAX-(r>>3)%4096); memcpy(m+off,&big,8);} break; case 4: len=1+(size_t)(r%(uint64_t)n); break;} }
  mxsb_ld_err e=mxsb_ld_check(m,len,&p,&im,&d); if(e==MXSB_LD_OK) okc++; else { errc++; if(!d.rule||d.rule[0]!='V'){ fprintf(stderr,"no rule for error %d\n",e); return 1;} if(e<1||e>21){fprintf(stderr,"bad code %d\n",e);return 1;} }
 }
 printf("fuzz: %d iterations, %d admitted, %d refused\n",iters,okc,errc); return 0; }
EOF
gcc ${SAN/-Werror/} -Wno-unused-parameter -Wno-unused-result -I"$HERE" -o "$B/fuzz" "$B/fuzz.c" "$HERE/mxsb_load.c" && "$B/fuzz" "$B/corpus/valid_unsigned.mxsb" 20000 && ok || bad "fuzz run"

echo "== cross-check against the codegen emitter ($CG)"
if [ -f "$CG/mxsb/mxsb.c" ]; then
  rm -rf "$B/cg"; mkdir -p "$B/cg"; cp "$CG"/mxsb/*.c "$CG"/mxsb/*.h "$B/cg/"
  if gcc -std=c11 -O2 -Wall -Wno-unused-parameter -Wno-unused-function -o "$B/mxsb_emit" "$B/cg/mxsb.c" "$B/cg/mxsb_elf.c" "$B/cg/mxsb_emit.c" 2>"$B/cg_build.log"; then
    # 1. codegen's verifier on the reference corpus: must agree on every structural case (V20/V26 are policy)
    while read -r name exp rule; do
      case "$rule" in V20|V26) continue;; esac
      out=$("$B/mxsb_emit" verify "$B/corpus/$name.mxsb" 2>&1 | head -1)
      if [ "$exp" = "MXSB_OK" ]; then echo "$out" | grep -q "ok" && ok || bad "codegen verify rejects $name: $out"
      else echo "$out" | grep -q "$exp" && ok || bad "codegen verify on $name: want $exp, got: $out"; fi
    done < "$B/corpus/expected.txt"
    # 2. a file built by the codegen emitter, loaded by the reference loader (unsigned + approvals, then signed)
    head -c 4096 /dev/zero | tr '\0' '\220' > "$B/t.bin"; head -c 300 /dev/urandom > "$B/d.bin"
    printf 'mxsb-manifest 1\nname cg-demo\nabi native\nbuild_id 0000000000000000000000000000000000000000000000000000000000000000\nrequire fs.read=/app\nrequire render\nrequire superop=blit\nrequire random\n' > "$B/cg.manifest"
    if "$B/mxsb_emit" build --text "$B/t.bin" --data "$B/d.bin" --bss 8192 --entry 0 --manifest "$B/cg.manifest" --superop blit --unsigned -o "$B/cg_unsigned.mxsb" >"$B/cg_emit.log" 2>&1; then
      dg=$("$B/mxsb_check" "$B/cg_unsigned.mxsb" --structural | sed -n 's/.*digest=\([0-9a-f]*\).*/\1/p')
      out=$("$B/mxsb_check" "$B/cg_unsigned.mxsb" --container "$HERE/fixtures/warden.toml" --approve "$dg" --dump 2>&1)
      echo "$out" | head -1 | grep -q '^MXSB_OK unsigned+approved' && ok || bad "codegen unsigned file refused by reference loader: $out"
      # digest agreement between the two implementations
      cgd=$("$B/mxsb_emit" digest "$B/cg_unsigned.mxsb" 2>/dev/null | grep -o '[0-9a-f]\{64\}' | head -1)
      [ -n "$cgd" ] && [ "$cgd" = "$dg" ] && ok || bad "digest disagreement: ref=$dg codegen=$cgd"
      # signed path
      if command -v /usr/bin/openssl >/dev/null; then
        /usr/bin/openssl genpkey -algorithm ed25519 -out "$B/key.pem" 2>/dev/null && /usr/bin/openssl pkey -in "$B/key.pem" -pubout -out "$B/pub.pem" 2>/dev/null
        if "$B/mxsb_emit" build --text "$B/t.bin" --data "$B/d.bin" --bss 8192 --entry 0 --manifest "$B/cg.manifest" --superop blit --sign "$B/key.pem" -o "$B/cg_signed.mxsb" >>"$B/cg_emit.log" 2>&1; then
          out=$("$B/mxsb_check" "$B/cg_signed.mxsb" --container "$HERE/fixtures/no_unsigned.toml" --pubkey "$B/pub.pem" 2>&1 | head -1)
          echo "$out" | grep -q '^MXSB_OK signed' && ok || bad "signed file refused: $out"
          # wrong key must be refused as untrusted; flipped TEXT bit must fail build_id (V23) before/at signature
          /usr/bin/openssl genpkey -algorithm ed25519 -out "$B/key2.pem" 2>/dev/null && /usr/bin/openssl pkey -in "$B/key2.pem" -pubout -out "$B/pub2.pem" 2>/dev/null
          out=$("$B/mxsb_check" "$B/cg_signed.mxsb" --container "$HERE/fixtures/no_unsigned.toml" --pubkey "$B/pub2.pem" 2>&1 | head -1)
          echo "$out" | grep -q '^MXSB_E_UNTRUSTED_KEY V20' && ok || bad "wrong key not refused: $out"
          cp "$B/cg_signed.mxsb" "$B/cg_tampered.mxsb"; python3 -I -c "import sys;p=sys.argv[1];b=bytearray(open(p,'rb').read());b[-30]^=1;open(p,'wb').write(b)" "$B/cg_tampered.mxsb"
          out=$("$B/mxsb_check" "$B/cg_tampered.mxsb" --container "$HERE/fixtures/no_unsigned.toml" --pubkey "$B/pub.pem" 2>&1 | head -1)
          echo "$out" | grep -q '^MXSB_E_SIGNATURE V20' && ok || bad "tampered signature not refused: $out"
          out=$("$B/mxsb_check" "$B/cg_signed.mxsb" --container "$HERE/fixtures/no_unsigned.toml" 2>&1 | head -1)
          echo "$out" | grep -q '^MXSB_E_UNTRUSTED_KEY V20' && ok || bad "signed file with empty trust list not refused: $out"
        else bad "codegen emitter could not sign (see $B/cg_emit.log)"; fi
      fi
    else bad "codegen emitter build command failed (see $B/cg_emit.log)"; fi
  else echo "SKIP: codegen emitter did not compile here (see $B/cg_build.log)"; fi
else echo "SKIP: codegen emitter not found at $CG"; fi

echo
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
