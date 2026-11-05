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
# Independent host reference uses the same source, not guest runtime.
# This is a consistency oracle, not proof of visual correctness.
cc -std=c11 -O2 -Wall -Wextra -Werror -o /tmp/warden-host-frame-oracle \
  tools/game-frame-probe/main.c tools/game-frame-probe/render.c
host_line=$(/tmp/warden-host-frame-oracle 60)
host_hex=$(sed -n 's/.* digest=\([0-9a-f]*\) .*/\1/p' <<< "$host_line")
[[ "$host_hex" =~ ^[0-9a-f]{16}$ ]] || { echo "invalid host oracle: $host_line" >&2; exit 1; }
for repeat in 1 2; do
  log="$(mktemp)"
  set +e
  timeout 40 qemu-system-x86_64 -machine pc,accel=tcg -smp 1 -m 128M -display none -vga std -serial file:"$log" \
    -no-reboot -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel "$kernel.game-probe.mb32"
  rc=$?
  set -e
  grep -E 'report pong 40|report pong 41|FAIL|KERNEL PANIC' "$log" | tail -30 || true
  # Pong-specific scoring currently expects its own telemetry; it is not a
  # correctness verdict for the substituted guest. The guest must still
  # actually report its frame count and match the independently compiled
  # host digest, then exit without panic or timeout.
  if ! grep -Eq '^report pong 40 60\r?$' "$log"; then
      echo "ERROR: native guest did not report 60 frames in run $repeat (qemu rc $rc)" >&2; exit 1
  fi
  guest_dec=$(sed -n 's/^report pong 41 \([0-9]*\)\r*$/\1/p' "$log" | tail -1)
  if [[ ! "$guest_dec" =~ ^[0-9]+$ ]]; then
      echo "ERROR: no guest digest in run $repeat" >&2; exit 1
  fi
  python3 - "$guest_dec" "$host_hex" <<'PY'
import sys
guest_decimal, host_hex = sys.argv[1:]
assert int(guest_decimal) == int(host_hex, 16), (
    f"guest {guest_decimal} != host reference {host_hex}")
PY
  if grep -q 'KERNEL PANIC:' "$log" || [[ "$rc" == 124 || "$rc" == 125 ]]; then
      echo "ERROR: panic or timeout in run $repeat (qemu rc $rc)" >&2; exit 1
  fi
  echo "RUN $repeat: Warden ring-3 produced 60 frames; guest hash matches host=$host_hex; QEMU exit=$rc"
done
echo "EXPERIMENTAL ONE-VCPU WARDEN GUEST: TWO CONSISTENT RUNS, HOST DIGEST MATCH; Pong-specific checks remain separate"
