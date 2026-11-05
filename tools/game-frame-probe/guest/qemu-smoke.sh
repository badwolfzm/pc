#!/usr/bin/env bash
# Experimental real ring-3 guest smoke run under one QEMU vCPU.
# Edits only this CI checkout's build adapter; never commits a kernel change.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"
test -f crates/warden-kernel/guest/pong/build.sh
test -f tools/game-frame-probe/guest/build.sh
# Feature pong currently hardcodes the blob filenames. Temporarily delegate
# its build to our standalone adapter in this disposable runner checkout.
cat > crates/warden-kernel/guest/pong/build.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$1"
bash "$ROOT/tools/game-frame-probe/guest/build.sh" "$OUT"
cp "$OUT/game-probe_text.bin" "$OUT/pong_text.bin"
cp "$OUT/game-probe_data.bin" "$OUT/pong_data.bin"
EOF
chmod +x crates/warden-kernel/guest/pong/build.sh
cargo build -q -p warden-kernel --target x86_64-unknown-none --profile release --features pong
kernel="target/x86_64-unknown-none/release/warden-kernel"
objcopy -I elf64-x86-64 -O elf32-i386 "$kernel" "$kernel.game-probe.mb32"
log="$(mktemp)"
set +e
timeout 40 qemu-system-x86_64 -machine pc,accel=tcg -smp 1 -m 128M -display none -vga std -serial file:"$log" \
  -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel "$kernel.game-probe.mb32"
rc=$?
set -e
grep -E 'report|PASS|FAIL|panic|PONG RUN|pong:|ALL HELD' "$log" | tail -90 || true
# The legacy Pong-specific scoring expects Pong's telemetry and may reject
# this different guest. Validate *only* the experimental adapter's explicit
# reports; never present this as the full kernel suite passing.
if ! grep -Eq '^report pong 40 60\r?$' "$log"; then
    echo "ERROR: native guest never reported 60 rendered frames (qemu rc $rc)" >&2; exit 1
fi
if ! grep -Eq '^report pong 41 [0-9]+\r?$' "$log"; then
    echo "ERROR: native guest never reported a frame digest" >&2; exit 1
fi
if grep -q 'KERNEL PANIC:' "$log"; then
    echo "ERROR: kernel panicked" >&2; exit 1
fi
if [[ "$rc" == 124 || "$rc" == 125 ]]; then
    echo "ERROR: QEMU timed out" >&2; exit 1
fi
echo "EXPERIMENTAL WARDEN GUEST REPORTS OBSERVED; the original Pong harness may have expected failures"
