#!/usr/bin/env bash
# MXSB end-to-end: unit/mutation/fuzz tests under sanitizers, then a real
# clang-built freestanding guest wrapped into an .mxsb, signed with an
# Ed25519 key, verified, and cross-checked by an INDEPENDENT Python
# implementation of the digest, build_id and ELF image rules.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="${WORK:-$(mktemp -d)}"; CLANG="${CLANG:-/usr/bin/clang}"
fails=0; pass=0
t() { if "$@"; then pass=$((pass+1)); else fails=$((fails+1)); echo "FAIL: $*"; fi; }
E="$WORK/mxsb_emit"
gcc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer -o "$WORK/mxsb_test" "$HERE/mxsb_test.c" "$HERE/../mxsb/mxsb.c" && t "$WORK/mxsb_test" 20000
gcc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer -o "$E" "$HERE/../mxsb/mxsb.c" "$HERE/../mxsb/mxsb_elf.c" "$HERE/../mxsb/mxsb_emit.c" || { echo "FAIL build emitter"; exit 1; }
export ASAN_OPTIONS=detect_leaks=0
cd "$WORK"
cat > guest.c <<'G'
static const char msg[] = "hello from mxsb guest\n";
static unsigned counter = 7; static unsigned scratch[1024];
static long sys3(long n, long a, long b, long c) { long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory"); return r; }
void _start(void) { scratch[3] = counter * 3; sys3(1, 1, (long)msg, sizeof msg - 1); sys3(60, (long)(scratch[3] - 21), 0, 0); for (;;) {} }
G
t "$CLANG" -O2 -static-pie -nostdlib -fPIE -fno-plt -fno-asynchronous-unwind-tables -fcf-protection=none -Wl,-z,separate-code -Wl,-z,max-page-size=4096 -o guest_pie guest.c
t "$CLANG" -O0 -static -nostdlib -fno-pie -fno-asynchronous-unwind-tables -Wl,-z,separate-code -Wl,-z,max-page-size=4096 -o guest_exec guest.c
t ./guest_pie; t ./guest_exec
cat > manifest.txt <<'M'
mxsb-manifest 1
name   demo-guest
abi    native
require fs.read=/sandbox/assets
require render
require superop=blit
require clock=coarse
require fs.read=/sandbox/assets   # duplicate, dropped
M
openssl genpkey -algorithm ed25519 -out key.pem 2>/dev/null; openssl pkey -in key.pem -pubout -out pub.pem; openssl genpkey -algorithm ed25519 -out other.pem 2>/dev/null; openssl pkey -in other.pem -pubout -out otherpub.pem
t "$E" build --elf guest_pie --manifest manifest.txt --superop blit --tier multi --sign key.pem -o pie.mxsb
t "$E" build --elf guest_exec --manifest manifest.txt --unsigned -o exec.mxsb
t "$E" verify pie.mxsb --pubkey pub.pem
t bash -c "! $E verify pie.mxsb --pubkey otherpub.pem 2>/dev/null"
t bash -c "! $E verify exec.mxsb --approve 00 2>/dev/null"
t "$E" verify exec.mxsb --approve "$("$E" digest exec.mxsb)"
t "$E" dump pie.mxsb
# tamper: one TEXT byte flips -> build_id refusal; one BUILD_INFO byte -> signature refusal
python3 -I - <<'P'
import struct
b=bytearray(open('pie.mxsb','rb').read()); off=struct.unpack_from('<Q',b,16)[0]; n=struct.unpack_from('<I',b,12)[0]
secs=[struct.unpack_from('<IIQQQ',b,off+32*i) for i in range(n)]
text=[s for s in secs if s[0]==2][0]; bi=[s for s in secs if s[0]==7][0]
t=bytearray(b); t[text[2]+5]^=1; open('tamper_text.mxsb','wb').write(t)
u=bytearray(b); u[bi[2]+3]^=1; open('tamper_bi.mxsb','wb').write(u)
P
t bash -c "$E verify tamper_text.mxsb --pubkey pub.pem 2>&1 | grep -q MXSB_E_BUILD_ID"
t bash -c "$E verify tamper_bi.mxsb --pubkey pub.pem 2>&1 | grep -q MXSB_E_SIGNATURE"
# refusals at the ELF boundary
t bash -c "! $E build --elf /bin/ls --manifest manifest.txt --unsigned -o x.mxsb 2>/dev/null"            # dynamically linked
t bash -c "! $E build --text guest.c --entry 0 --manifest <(printf 'mxsb-manifest 1\nname a\nabi native\nrequire superop=blit\n') --superop clear --unsigned -o x.mxsb 2>/dev/null"  # superop not required
# v1 decode (tools only) and upgrade
python3 -I - <<'P'
import struct
text=bytes((i^0xA5)&0xff for i in range(64)); data=bytes((i*3)&0xff for i in range(32))
h=struct.pack('<4sHHIIIIQQ',b'MXSB',1,0,len(text),len(data),2,0x11,40,0)
open('v1.mxsb','wb').write(h+text+data)
P
t bash -c "$E dump v1.mxsb | grep -q 'v1: tools decode, loader refuses'"
t bash -c "$E verify v1.mxsb 2>&1 | grep -q MXSB_E_VERSION"
t "$E" upgrade v1.mxsb --manifest manifest.txt --unsigned -o v1up.mxsb
t "$E" verify v1up.mxsb
# independent cross-check in Python: digest, build_id, ELF image rule
python3 -I - "$WORK" <<'P'
import struct, hashlib, sys, subprocess
w=sys.argv[1]
def check(name, cond):
    print(("ok   " if cond else "FAIL ")+name); return cond
ok=True
for f in ('pie.mxsb','exec.mxsb'):
    b=open(f,'rb').read()
    magic,maj,mino,hsz,flags,cnt,toff,entry,caps,fsz=struct.unpack_from('<4sBBHIIQQQQ',b,0)
    ok&=check(f+': header basics', magic==b'MXSB' and maj==2 and hsz==64 and fsz==len(b) and toff==64)
    secs=[struct.unpack_from('<IIQQQ',b,toff+32*i) for i in range(cnt)]
    sig=[s for s in secs if s[0]==6][0]
    dig=hashlib.sha256(b[:sig[2]]+b[sig[2]+sig[3]:])      # 2.0.3: whole file minus SIGNATURE bytes
    covered=bytearray(len(b)); covered[0:64]=b'\x01'*64; covered[toff:toff+32*cnt]=b'\x01'*(32*cnt)
    for ty,fl,off,sz,res in secs:
        if ty!=4: covered[off:off+sz]=b'\x01'*sz
    ok&=check(f+': V27 padding bytes are zero', all(b[i]==0 for i in range(len(b)) if not covered[i]))
    tool=subprocess.run([w+'/mxsb_emit','digest',f],capture_output=True,text=True).stdout.strip()
    ok&=check(f+': digest matches independent SHA-256', dig.hexdigest()==tool)
    text=[s for s in secs if s[0]==2][0]; data=[s for s in secs if s[0]==3]
    tb=b[text[2]:text[2]+text[3]]; db=b[data[0][2]:data[0][2]+data[0][3]] if data else b''
    man=[s for s in secs if s[0]==1][0]; mtxt=b[man[2]:man[2]+man[3]].decode()
    bid=[l.split()[1] for l in mtxt.splitlines() if l.startswith('build_id')][0]
    ok&=check(f+': build_id == sha256(TEXT||DATA)', hashlib.sha256(tb+db).hexdigest()==bid)
    # ELF image rule, recomputed from program headers
    elf=open('guest_pie' if f=='pie.mxsb' else 'guest_exec','rb').read()
    e_entry,e_phoff=struct.unpack_from('<QQ',elf,24); phnum=struct.unpack_from('<H',elf,56)[0]
    loads=[]
    for i in range(phnum):
        p_type,p_flags,p_off,p_vaddr,p_paddr,p_filesz,p_memsz,p_align=struct.unpack_from('<IIQQQQQQ',elf,e_phoff+56*i)
        if p_type==1 and p_memsz: loads.append((p_vaddr,p_off,p_filesz,p_memsz,p_flags))
    loads.sort(); base=loads[0][0]&~0xfff
    w_segs=[l for l in loads if l[4]&2]
    data_start=(w_segs[0][0]&~0xfff) if w_segs else None
    text_end=max(l[0]+l[3] for l in loads if not l[4]&2)
    tsize=(data_start-base) if w_segs else (((text_end+0xfff)&~0xfff)-base)
    img=bytearray(tsize)
    for v,o,fs,ms,fl in loads:
        if fl&2: continue
        img[v-base:v-base+fs]=elf[o:o+fs]
    if len(img)<len(tb): img+=bytes(len(tb)-len(img))   # emitter pads TEXT to a page when BSS exists without DATA
    ok&=check(f+': TEXT image equals independent ELF reconstruction', bytes(img)==tb)
    ok&=check(f+': V28 TEXT page multiple when DATA/BSS exists', (not data and not any(s[0]==4 for s in secs)) or len(tb)%4096==0)
    ok&=check(f+': entry_off inside TEXT at e_entry-base', entry==text[2]+(e_entry-base))
    if w_segs:
        dend=max(v+fs for v,o,fs,ms,fl in w_segs); dimg=bytearray(dend-data_start)
        for v,o,fs,ms,fl in w_segs: dimg[v-data_start:v-data_start+fs]=elf[o:o+fs]
        ok&=check(f+': DATA image equals independent ELF reconstruction', bytes(dimg)==db)
        ok&=check(f+': TEXT.size page aligned when DATA present', tsize%4096==0)
    ok&=check(f+': caps_summary == OR of family bits', caps==(1<<0)|(1<<3)|(1<<4)|(1<<5))
sys.exit(0 if ok else 1)
P
t test $? -eq 0
echo "run_mxsb: $pass passed, $fails failed (work dir $WORK)"
[[ $fails -eq 0 ]]
