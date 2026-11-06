#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 ]]; then
  echo 'usage: bash tools/doom-real-wfile-link/run.sh /path/to/pinned-upstream/doomgeneric' >&2
  exit 2
fi
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UP="$(realpath "$1")"
PIN=e257a09527062bfb986b6e5e0780839b6cbd5b71
REV="$(git -C "$UP" rev-parse HEAD)"
[[ "$REV" == "$PIN" ]] || { echo "wrong upstream revision: $REV" >&2; exit 1; }
for f in w_file.c w_file.h doomtype.h config.h m_argv.h; do
  [[ -f "$UP/$f" ]] || { echo "missing genuine upstream file: $f" >&2; exit 1; }
done
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
COMMON=(
  -std=gnu11 -O2 -g -Wall -Wextra -Werror
  -I"$UP" -I"$ROOT/tools/doom-wad-class-bridge" -I"$ROOT/tools/doom-wad-stream"
  "$UP/w_file.c"
  "$ROOT/tools/doom-wad-class-bridge/doom_wad_bridge.c"
  "$ROOT/tools/doom-wad-stream/wad_stream.c"
  "$ROOT/tools/doom-real-wfile-link/bridge_backend.c"
  "$ROOT/tools/doom-real-wfile-link/test_upstream.c"
)
cc "${COMMON[@]}" -o "$OUT/upstream-wfile"
"$OUT/upstream-wfile"
cc "${COMMON[@]}" -O1 -fsanitize=undefined -fno-sanitize-recover=all -o "$OUT/upstream-wfile-ubsan"
"$OUT/upstream-wfile-ubsan"
