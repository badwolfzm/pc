#!/usr/bin/env bash
# Build the Pong guest app into two flat blobs the kernel loads:
#   $1/pong_text.bin  -> 0x800000, R-X      $1/pong_data.bin -> 0x840000, RW (then zeroed bss/heap pages)
# Run by build.rs when the `pong` feature is on. Needs gcc, ld, objcopy.
set -euo pipefail
OUT="$1"; HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../../../.." && pwd)"
CANVAS="$ROOT/matrixos/canvas"
CFLAGS="-std=c11 -O2 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=small -Wall -Wextra -Wno-misleading-indentation -I$HERE -I$CANVAS"
mkdir -p "$OUT"
gcc $CFLAGS -c "$HERE/start.S" -o "$OUT/start.o"
gcc $CFLAGS -c "$HERE/rt.c" -o "$OUT/rt.o"
gcc $CFLAGS -c "$HERE/pong.c" -o "$OUT/pong.o"
gcc $CFLAGS -Wno-unused-function -c "$CANVAS/ghost_canvas.c" -o "$OUT/ghost_canvas.o"
ld -nostdlib -static -T "$HERE/guest.ld" -o "$OUT/pong.elf" "$OUT/start.o" "$OUT/rt.o" "$OUT/pong.o" "$OUT/ghost_canvas.o"
objcopy -O binary -j .text -j .rodata "$OUT/pong.elf" "$OUT/pong_text.bin"
objcopy -O binary -j .data "$OUT/pong.elf" "$OUT/pong_data.bin"
T=$(stat -c %s "$OUT/pong_text.bin"); D=$(stat -c %s "$OUT/pong_data.bin")
[ "$T" -le $((0x40000)) ] || { echo "pong text+rodata is $T bytes, over the 256 KiB code region" >&2; exit 1; }
[ "$D" -le $((0x10000)) ] || { echo "pong .data is $D bytes, over 64 KiB" >&2; exit 1; }
echo "pong guest: text+rodata $T bytes, data $D bytes"
