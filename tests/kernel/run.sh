#!/usr/bin/env bash
# Build the Phase 1 kernel and run its in-guest escape suite under QEMU.
# Exit 0 when every check held. Uses KVM when /dev/kvm exists, else TCG.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PROFILE="${PROFILE:-release}"
cargo build -q -p warden-kernel --target x86_64-unknown-none --profile "$PROFILE"
DIR="$PROFILE"; [[ "$PROFILE" == dev ]] && DIR=debug
K="target/x86_64-unknown-none/$DIR/warden-kernel"
# QEMU's -kernel loader takes only ELF32 multiboot images; the code is 64-bit.
objcopy -I elf64-x86-64 -O elf32-i386 "$K" "$K.mb32"
ACCEL=tcg; [[ -w /dev/kvm ]] && ACCEL=kvm
set +e
timeout "${TIMEOUT:-120}" qemu-system-x86_64 -machine pc,accel="$ACCEL" -m 128M -display none -serial stdio \
    -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel "$K.mb32"
RC=$?
set -e
# isa-debug-exit maps our code c to exit status (c << 1) | 1: 1 = all held.
case $RC in
  1) echo "kernel escape suite: ALL HELD"; exit 0 ;;
  3) echo "kernel escape suite: FAILED CHECKS"; exit 1 ;;
  *) echo "kernel escape suite: kernel did not finish (qemu status $RC)"; exit 2 ;;
esac
