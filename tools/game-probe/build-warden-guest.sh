#!/usr/bin/env bash
# Compile a guest matching the existing Warden Pong flat-blob layout.
# Does not modify kernel sources or replace the Pong binaries.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/tools/game-probe"
PONG="$ROOT/crates/warden-kernel/guest/pong"
OUT="${1:-/tmp/warden-g0-guest}"
mkdir -p "$OUT"
CC="${CC:-gcc}"
FLAGS=(-std=c11 -O2 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector
 -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=small
 -Wall -Wextra -Werror -I"$PONG" -I"$HERE")
"$CC" "${FLAGS[@]}" -c "$PONG/start.S" -o "$OUT/start.o"
for part in probe transport game_loop warden_bridge warden_guest; do
 "$CC" "${FLAGS[@]}" -c "$HERE/$part.c" -o "$OUT/$part.o"
done
ld -nostdlib -static -T "$PONG/guest.ld" -o "$OUT/game.elf" \
 "$OUT/start.o" "$OUT/probe.o" "$OUT/transport.o" \
 "$OUT/game_loop.o" "$OUT/warden_bridge.o" "$OUT/warden_guest.o"
objcopy -O binary -j .text -j .rodata "$OUT/game.elf" "$OUT/game_text.bin"
objcopy -O binary -j .data "$OUT/game.elf" "$OUT/game_data.bin"
T=$(stat -c %s "$OUT/game_text.bin")
D=$(stat -c %s "$OUT/game_data.bin")
[ "$T" -le $((0x40000)) ] || { echo "text exceeds 256KiB region" >&2; exit 1; }
[ "$D" -le $((0x10000)) ] || { echo "data exceeds 64KiB region" >&2; exit 1; }
# BSS included in mapped 0x840000..0x9e0000; reject any overflow.
BSS_END=$(nm -n "$OUT/game.elf" | awk '$2=="B" || $2=="b" {print $1}' | tail -1)
if [ -n "$BSS_END" ]; then
 [ $((16#$BSS_END)) -lt $((0x9e0000)) ] || { echo "BSS beyond guest heap end" >&2; exit 1; }
fi
echo "PASS guest native ELF linked: text=$T bytes data=$D bytes (not QEMU executed)"
