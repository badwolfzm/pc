#!/usr/bin/env bash
# Issue one private operator CA, one controller cert and a different server
# certificate per Warden host. Keep the CA private key offline afterward.
# Usage: bash bootstrap-certs.sh OUTPUT_DIR HOST1_OR_IP [HOST2_OR_IP ...]
set -euo pipefail
umask 077
out="$1"
shift
if (( $# < 1 || $# > 32 )); then
  echo "expected between one and 32 worker hostnames/IP addresses" >&2
  exit 2
fi
if [[ -e "$out" ]]; then
  echo "refusing to overwrite existing certificate directory" >&2
  exit 2
fi
mkdir -m 700 "$out"
out="$(realpath "$out")"
openssl req -x509 -newkey rsa:3072 -sha256 -noenc -days 180 \
  -subj "/CN=private-Warden-fabric-CA" \
  -keyout "$out/ca.key" -out "$out/ca.pem" >/dev/null 2>&1
openssl req -new -newkey rsa:3072 -noenc -sha256 \
  -subj "/CN=warden-controller" \
  -keyout "$out/controller.key" -out "$out/controller.csr" >/dev/null 2>&1
printf 'extendedKeyUsage=clientAuth\n' > "$out/controller.ext"
openssl x509 -req -sha256 -days 90 -in "$out/controller.csr" \
  -CA "$out/ca.pem" -CAkey "$out/ca.key" -CAcreateserial \
  -extfile "$out/controller.ext" -out "$out/controller.pem" >/dev/null 2>&1
n=0
for host in "$@"; do
  n=$((n + 1))
  if [[ ! "$host" =~ ^[A-Za-z0-9_.:-]+$ ]]; then
    echo "invalid hostname/IP for worker $n" >&2
    exit 2
  fi
  if [[ "$host" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    san="IP:$host"
  elif [[ "$host" == *:* ]]; then
    san="IP:$host"
  else
    san="DNS:$host"
  fi
  openssl req -new -newkey rsa:3072 -sha256 -noenc \
    -subj "/CN=warden-node-$n" \
    -keyout "$out/node-$n.key" -out "$out/node-$n.csr" >/dev/null 2>&1
  printf 'subjectAltName=%s\nextendedKeyUsage=serverAuth\n' "$san" > "$out/node-$n.ext"
  openssl x509 -req -sha256 -days 90 -in "$out/node-$n.csr" \
    -CA "$out/ca.pem" -CAkey "$out/ca.key" -CAcreateserial \
    -extfile "$out/node-$n.ext" -out "$out/node-$n.pem" >/dev/null 2>&1
  printf 'Node %d identity for %s: node-%d.{key,pem}\n' "$n" "$host" "$n"
done
echo "Created private CA and controller identity in: $out"
echo "NEVER copy ca.key to a worker. Transfer each node's own key/cert and ca.pem securely."
echo "Protect controller.key; each authorized client certificate can submit approved jobs."
