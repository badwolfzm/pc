#!/usr/bin/env bash
# Build only; does NOT boot or replace Pong. Requires PR #68's renderer sources.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/target/warden-game-guest}"
PONG="$ROOT/crates/warden-kernel/guest/pong"
GAME="$ROOT/tools/game-frame-probe"
HERE="$ROOT/tools/warden-game-guest"
mkdir -p "$OUT"
CC="${CC:-gcc}"
LD="${LD:-ld}"
OBJCOPY="${OBJCOPY:-objcopy}"
CFLAGS=(-std=c11 -O2 -ffreestanding -nostdlib -fno-builtin
  -fno-stack-protector -fno-pic -fno-pie -mno-red-zone
  -mgeneral-regs-only -mcmodel=small -Wall -Wextra -Werror -Wpedantic)
"$CC" "${CFLAGS[@]}" -c "$PONG/start.S" -o "$OUT/start.o"
"$CC" "${CFLAGS[@]}" -c "$HERE/guest.c" -o "$OUT/guest.o"
"$CC" "${CFLAGS[@]}" -c "$GAME/render.c" -o "$OUT/render.o"
"$LD" -nostdlib -static -T "$PONG/guest.ld" -o "$OUT/game.elf" \
  "$OUT/start.o" "$OUT/guest.o" "$OUT/render.o"
"$OBJCOPY" -O binary -j .text -j .rodata "$OUT/game.elf" "$OUT/game_text.bin"
"$OBJCOPY" -O binary -j .data "$OUT/game.elf" "$OUT/game_data.bin"
T=$(wc -c < "$OUT/game_text.bin")
D=$(wc -c < "$OUT/game_data.bin")
[[ "$T" -le 262144 ]] || { echo "text too large: $T bytes" >&2; exit 1; }
[[ "$D" -le 65536 ]] || { echo "initialized data too large: $D bytes" >&2; exit 1; }
echo "PASS: built freestanding Warden game adapter: text=$T data=$D; NOT BOOTED"
