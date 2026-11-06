#!/usr/bin/env bash
# Exercise two real Warden QEMU workers on separate loopback TLS endpoints.
# This is a trusted-machine orchestration test, NOT guest-native networking.
set -euo pipefail
cd "$(dirname "$0")/../.."
image="$1"
image="$(realpath "$image")"
if [[ ! -r "$image" ]]; then echo "kernel image missing" >&2; exit 2; fi
qemu="$(command -v qemu-system-x86_64)"
sha="$(sha256sum "$image" | cut -d' ' -f1)"
temp="$(mktemp -d)"
pids=""
cleanup() {
  for pid in $pids; do kill "$pid" 2>/dev/null || :; done
  for pid in $pids; do wait "$pid" 2>/dev/null || :; done
  rm -rf "$temp"
}
trap cleanup EXIT
# Test-only ephemeral CA. Production nodes use separately protected keys.
openssl req -new -x509 -newkey rsa:2048 -noenc -days 1 \
  -subj "/CN=warden-fabric-ci-ca" -keyout "$temp/ca.key" -out "$temp/ca.pem" >/dev/null 2>&1
openssl req -new -newkey rsa:2048 -noenc \
  -subj "/CN=localhost" -keyout "$temp/server.key" -out "$temp/server.csr" >/dev/null 2>&1
cat > "$temp/server.ext" <<'EOF'
subjectAltName=IP:127.0.0.1
extendedKeyUsage=serverAuth
EOF
openssl x509 -req -in "$temp/server.csr" -CA "$temp/ca.pem" -CAkey "$temp/ca.key" \
  -CAcreateserial -out "$temp/server.pem" -days 1 -extfile "$temp/server.ext" >/dev/null 2>&1
openssl req -new -newkey rsa:2048 -noenc \
  -subj "/CN=warden-fabric-ci-controller" -keyout "$temp/client.key" -out "$temp/client.csr" >/dev/null 2>&1
echo "extendedKeyUsage=clientAuth" > "$temp/client.ext"
openssl x509 -req -in "$temp/client.csr" -CA "$temp/ca.pem" -CAkey "$temp/ca.key" \
  -CAcreateserial -out "$temp/client.pem" -days 1 -extfile "$temp/client.ext" >/dev/null 2>&1
for port in 19443 19444; do
  python3 tools/warden-cpu-fabric/fabric.py worker \
    --qemu "$qemu" --image "$image" --image-sha256 "$sha" \
    --accel tcg --slots 1 --timeout 40 --checks 201 --memory-mib 512 \
    --listen 127.0.0.1 --port "$port" \
    --tls-ca "$temp/ca.pem" \
    --tls-cert "$temp/server.pem" --tls-key "$temp/server.key" \
    > "$temp/worker-$port.log" 2>&1 &
  pids="$pids $!"
done
printf '{"nodes":["https://127.0.0.1:19443","https://127.0.0.1:19444"]}\n' > "$temp/nodes.json"
for port in 19443 19444; do
  ok=0
  for _ in {1..40}; do
    if curl -fsS --connect-timeout 1 --max-time 2 \
        --cert "$temp/client.pem" --key "$temp/client.key" --cacert "$temp/ca.pem" \
        "https://127.0.0.1:$port/v1/health" > "$temp/health-$port.json" 2>/dev/null; then
      ok=1; break
    fi
    sleep .2
  done
  if [[ "$ok" != 1 ]]; then cat "$temp/worker-$port.log"; exit 1; fi
  grep -q "\"image_sha256\":\"$sha\"" "$temp/health-$port.json"
done
python3 tools/warden-cpu-fabric/fabric.py run \
  --nodes "$temp/nodes.json" --image-sha256 "$sha" \
  --jobs 4 --parallel 2 --timeout 50 \
  --tls-ca "$temp/ca.pem" \
  --tls-cert "$temp/client.pem" --tls-key "$temp/client.key" \
  | tee "$temp/results.jsonl"
grep -q '^FABRIC SUMMARY: 4/4 passed$' "$temp/results.jsonl"
grep -q 'https://127.0.0.1:19443' "$temp/results.jsonl"
grep -q 'https://127.0.0.1:19444' "$temp/results.jsonl"
echo "TWO-NODE WARDEN FABRIC PASS: 4 real QEMU workloads across two mTLS nodes"
