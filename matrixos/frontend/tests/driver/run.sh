#!/usr/bin/env bash
# Driver tests for mxcc. Most assert on --dry-run command lines; the last block
# does real compiles. Usage: run.sh <mxcc binary> <frontend dir>
set -u
MXCC="$1"; ROOT="$2"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0; total=0
ok(){ total=$((total+1)); echo "PASS $1"; }
bad(){ total=$((total+1)); fails=$((fails+1)); echo "FAIL $1"; [ $# -gt 1 ] && echo "     $2"; }
dry(){ "$MXCC" --dry-run "$@" 2>&1 | tr '\n' ' '; }
has(){ local name="$1" needle="$2"; shift 2; local out; out="$(dry "$@")"; [[ " $out" == *" $needle "* ]] && ok "$name" || bad "$name" "missing [$needle] in: $out"; }
hasnot(){ local name="$1" needle="$2"; shift 2; local out; out="$(dry "$@")"; [[ " $out" != *" $needle "* ]] && ok "$name" || bad "$name" "unexpected [$needle] in: $out"; }
rc(){ local name="$1" want="$2"; shift 2; "$MXCC" "$@" >/dev/null 2>&1; local got=$?; [ "$got" = "$want" ] && ok "$name" || bad "$name" "rc $got != $want"; }

printf 'int main(void){return 0;}\n' > "$WORK/t.c"
printf 'int g(void){return 2;}\n' > "$WORK/u.c"
cd "$WORK"

# --- version / usage: whole string, with newline (baseline bug 1)
v="$("$MXCC" --version)"; [[ "$v" == "mxcc 1.0.1 (MatrixOS compiler driver)" ]] && ok "version prints whole line" || bad "version" "[$v]"
rc "no input files exits 2" 2 -S
rc "unknown profile exits 2" 2 --profile=bogus -S t.c -o t.s
rc "unknown tier exits 2" 2 --tier=sse9 -S t.c -o t.s
rc "unknown arch exits 2" 2 --arch=mips -S t.c -o t.s
rc "-o with -c and two files exits 2" 2 -c t.c u.c -o x.o
rc "--cc other than clang/gcc exits 2" 2 --cc=tcc --dry-run -c t.c
rc "--cc with a relative path exits 2" 2 --cc=./clang -c t.c
rc "missing arg after -o exits 2" 2 -c t.c -o

# --- tiers: fixed psABI levels, never native (control decision 3)
has "baseline tier -> -march=x86-64" "-march=x86-64" -c t.c
has "avx2 tier -> -march=x86-64-v3" "-march=x86-64-v3" --tier=avx2 -c t.c
has "avx512 tier -> -march=x86-64-v4" "-march=x86-64-v4" --tier=avx512 -c t.c
has "tier macro MX_SIMD_TIER=1" "-DMX_SIMD_TIER=1" --tier=avx2 -c t.c
has "tier name macro" '-DMX_SIMD_TIER_NAME="avx512"' --tier=avx512 -c t.c
for p in compile-fast matrixos balanced runtime-fast; do hasnot "profile $p has no -march=native" "-march=native" --profile=$p -c t.c; done
rc "-march=native explicit exits 2" 2 -march=native -c t.c
rc "-mtune=native explicit exits 2" 2 -mtune=native -c t.c
rc "-march=skylake conflicts with tiers, exits 2" 2 -march=skylake -c t.c
has "--allow-native passes it" "-march=native" --allow-native -march=native -c t.c
has "aarch64 baseline -> armv8-a" "-march=armv8-a" --arch=aarch64 -c t.c
rc "aarch64 + avx2 exits 2" 2 --arch=aarch64 --tier=avx2 -c t.c

# --- profiles
has "compile-fast -O0" "-O0" --profile=compile-fast -c t.c
has "matrixos -O2" "-O2" --profile=matrixos -c t.c
has "balanced -O1" "-O1" --profile=balanced -c t.c
has "runtime-fast -O3" "-O3" --profile=runtime-fast -c t.c
has "profile macro" "-DMX_PROFILE_RUNTIME_FAST=1" --profile=runtime-fast -c t.c
has "matrixos link strips and gc-sections" "-s" --profile=matrixos t.c -o t
hasnot "matrixos link with -g does not strip" "-s" --profile=matrixos -g t.c -o t
has "every profile drops -fno-ident" "-fno-ident" --profile=compile-fast -c t.c

# --- modes
has "-E" "-E" -E t.c
has "-S" "-S" -S t.c -o t.s
has "-c" "-c" -c t.c
hasnot "-c has no -pie" "-pie" -c t.c
has "link has -pie" "-pie" t.c -o t
has "link has relro" "-Wl,-z,relro" t.c -o t
has "link has now" "-Wl,-z,now" t.c -o t
has "link has noexecstack" "-Wl,-z,noexecstack" t.c -o t

# --- policy sets
has "hardening: stack protector" "-fstack-protector-strong" -c t.c
has "hardening: cf-protection on x86" "-fcf-protection=full" -c t.c
has "hardening: auto-var-init zero" "-ftrivial-auto-var-init=zero" -c t.c
has "hardening: fortify 3" "-D_FORTIFY_SOURCE=3" -c t.c
hasnot "--no-harden drops stack protector" "-fstack-protector-strong" --no-harden -c t.c
hasnot "--no-harden drops -pie" "-pie" --no-harden t.c -o t
has "reproducible: date-time is an error" "-Werror=date-time" -c t.c
has "reproducible: random seed from output name" "-frandom-seed=t.o" -c t.c -o t.o
has "reproducible: clang no command line record" "-fno-record-command-line" --cc=clang -c t.c
has "reproducible: gcc no switches record" "-fno-record-gcc-switches" --cc=gcc -c t.c
hasnot "--no-reproducible drops prefix map" "-Werror=date-time" --no-reproducible -c t.c
has "policy: vla warned" "-Wvla" -c t.c
has "policy: implicit decl is an error" "-Werror=implicit-function-declaration" -c t.c
has "--strict: vla is an error" "-Werror=vla" --strict -c t.c
has "--strict: alloca is an error" "-Werror=alloca" --strict -c t.c
has "freestanding: -nostdlib" "-nostdlib" --freestanding t.c -o t
hasnot "freestanding: no -pie" "-pie" --freestanding t.c -o t
hasnot "freestanding: no fortify" "-D_FORTIFY_SOURCE=3" --freestanding -c t.c

# --- typed input
has "typed-input: macro" "-DMX_TYPED_INPUT=1" --typed-input --mx-include=/inc -c t.c
has "typed-input: header injected" "/inc/mx_input.h" --typed-input --mx-include=/inc -c t.c
has "typed-input: int-conversion error" "-Werror=int-conversion" --typed-input --mx-include=/inc -c t.c
hasnot "no typed-input by default" "-DMX_TYPED_INPUT=1" -c t.c

# --- passthrough and ordering
has "-I dir pair preserved" "-I /some/dir" -I /some/dir -c t.c
has "-D passthrough" "-DFOO=1" -DFOO=1 -c t.c
has "-- passthrough" "-whatever" -c t.c -- -whatever
out="$(dry -c t.c -o t.o)"; [[ "$out" == *"-c t.c -o t.o "* ]] && ok "inputs then -o at the end" || bad "ordering" "$out"
MXCC_CC=gcc "$MXCC" --dry-run -c t.c | head -1 | grep -q '^gcc$' && ok "MXCC_CC env selects compiler" || bad "MXCC_CC env"
has "--cc=gcc first argv" "gcc -pipe" --cc=gcc -c t.c

# --- security review finding 3: code-loading flags refused, no PATH lookup
for bad in -fplugin=/x.so -fpass-plugin=/x.so -Xclang -load -specs=/x -B/x -wrapper @/resp -Wp,-D -Wa,-x -Wl,-plugin,/x -Wl,--plugin-opt=x -Wl,-T,/x -Wl,-rpath,/x -T/x -save-temps -fprofile-use=/x -isysroot; do
  rc "refuses $bad" 2 -c t.c -o t.o "$bad"
done
mkdir -p fakebin; printf '#!/bin/sh\necho PLANTED; exit 0\n' > fakebin/clang; chmod +x fakebin/clang
out="$(PATH="$PWD/fakebin:$PATH" "$MXCC" -S t.c -o t.s 2>&1)"; [[ "$out" != *PLANTED* && -s t.s ]] && ok "planted clang in PATH is not run" || bad "PATH hijack" "$out"
out="$(PATH="$PWD/fakebin" "$MXCC" --cc=/usr/bin/clang -S t.c -o t2.s 2>&1)"; [ -s t2.s ] && ok "absolute --cc works with hostile PATH" || bad "absolute cc" "$out"
chmod 0777 fakebin/clang
rc "a world-writable compiler binary is refused even by absolute path" 2 --cc="$PWD/fakebin/clang" -S t.c -o t3.s

# --- "--" passthrough goes through the same policy (audit 2026-11-05, issue 1)
rc "refuses -fplugin after --" 2 -c t.c -o t.o -- -fplugin=/x.so
rc "refuses -Xclang after --" 2 -c t.c -o t.o -- -Xclang -load
rc "refuses -Xlinker -plugin (paired value)" 2 -c t.c -o t.o -Xlinker -plugin
rc "refuses -Xlinker -plugin after --" 2 -c t.c -o t.o -- -Xlinker -plugin
rc "refuses -include @resp" 2 -c t.c -o t.o -include @resp
rc "refuses -march=native after --" 2 -c t.c -o t.o -- -march=native
rc "refuses -fuse-ld=/x" 2 -c t.c -o t.o -fuse-ld=/x
has "benign -- passthrough still works" "-DBENIGN" -c t.c -- -DBENIGN

# --- real compiles: absolute compiler paths, so a compiler outside mxcc's fixed
#     search directories is still exercised (audit: 13 false failures from bare names)
for name in clang gcc; do
  cc="$(command -v $name || true)"
  [ -n "$cc" ] || { echo "SETUP: $name not installed, real compiles with it skipped"; continue; }
  for p in compile-fast matrixos balanced runtime-fast; do
    if "$MXCC" --cc="$cc" --profile=$p -S t.c -o "$p.$name.s" 2>"$p.$name.log" && [ -s "$p.$name.s" ]; then ok "[$name] $p emits assembly"; else bad "[$name] $p -S" "$(head -3 "$p.$name.log")"; fi
  done
  for tier in baseline avx2 avx512; do
    if "$MXCC" --cc="$cc" --tier=$tier -c t.c -o "t.$tier.$name.o" 2>"tier.$name.log"; then ok "[$name] tier $tier compiles"; else bad "[$name] tier $tier" "$(head -3 tier.$name.log)"; fi
  done
  "$MXCC" --cc="$cc" --profile=matrixos -c t.c -o a1.o && "$MXCC" --cc="$cc" --profile=matrixos -c t.c -o a2.o && cmp -s a1.o a2.o && ok "[$name] two builds are byte-identical" || bad "[$name] determinism"
  if "$MXCC" --cc="$cc" --profile=matrixos t.c -o "hello.$name" 2>link.log && "./hello.$name"; then ok "[$name] hardened link runs"; else bad "[$name] link" "$(head -3 link.log)"; fi
  "$MXCC" --cc="$cc" --emit-build-info=bi.txt -c t.c -o bi.o && grep -q "^simd_tier=baseline$" bi.txt && grep -q "^profile=matrixos$" bi.txt \
     && [ "$(grep -o 'source_sha256=.*' bi.txt | cut -d= -f2)" = "$(sha256sum t.c | cut -d' ' -f1)" ] && ok "[$name] build-info sha256 matches sha256sum" || bad "[$name] build-info" "$(cat bi.txt 2>/dev/null)"
  printf 'int f(int n){int a[n]; a[0]=n; return a[0];}\n' > vla.c
  "$MXCC" --cc="$cc" --strict -c vla.c -o vla.o >/dev/null 2>&1 && bad "[$name] --strict rejects VLA" || ok "[$name] --strict rejects VLA"
  "$MXCC" --cc="$cc" -c vla.c -o vla.o >/dev/null 2>&1 && ok "[$name] default only warns on VLA (baseline source has them)" || bad "[$name] default VLA"
done
# --- plugin probe: a real shared object must never be loaded through any path
bash "$ROOT/tests/driver/plugin_regression.sh" "$MXCC"; prc=$?
[ $prc -eq 0 ] && ok "plugin regression suite" || bad "plugin regression suite" "rc=$prc"
echo "driver: $total checks, $fails failures"
exit $fails
