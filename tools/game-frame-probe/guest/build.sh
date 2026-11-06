#!/usr/bin/env bash
# Produce Warden-compatible flat code/data blobs WITHOUT modifying kernel.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="${1:-/tmp/warden-game-probe-native}"
mkdir -p "$OUT"
CC="${CC:-cc}"
FLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only)
"$CC" "${FLAGS[@]}" -c "$ROOT/tools/game-frame-probe/render.c" -o "$OUT/render.o"
"$CC" "${FLAGS[@]}" -c "$ROOT/tools/game-frame-probe/guest/guest.c" -o "$OUT/guest.o"
"$CC" -c "$ROOT/tools/game-frame-probe/guest/start.S" -o "$OUT/start.o"
ld -nostdlib -static -T "$ROOT/crates/warden-kernel/guest/pong/guest.ld" -o "$OUT/game-probe.elf" "$OUT/start.o" "$OUT/guest.o" "$OUT/render.o"
objcopy -O binary -j .text -j .rodata "$OUT/game-probe.elf" "$OUT/game-probe_text.bin"
# No .data content expected; output can be empty while framebuffer lives in bss.
objcopy -O binary -j .data "$OUT/game-probe.elf" "$OUT/game-probe_data.bin"
readelf -h "$OUT/game-probe.elf" | grep -q 'Machine:.*Advanced Micro Devices X86-64'
readelf -h "$OUT/game-probe.elf" | grep -q 'Entry point address:.*0x800000'
readelf -s "$OUT/game-probe.elf" | grep -q 'game_probe_main'
code_bytes=$(stat -c %s "$OUT/game-probe_text.bin")
data_bytes=$(stat -c %s "$OUT/game-probe_data.bin")
test "$code_bytes" -le 262144
test "$data_bytes" -le 65536
# Ensure that the mapped guest bss fits the Warden data+heap window.
bss_hex=$(readelf -SW "$OUT/game-probe.elf" | awk '$3==".bss" {print $7}')
if [[ -n "$bss_hex" ]]; then test "$((16#$bss_hex))" -le $((0x9E0000-0x840000)); fi
echo "native guest ELF compiled; text=$code_bytes data=$data_bytes (NOT booted)"
