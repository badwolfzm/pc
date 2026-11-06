#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UP="${1:-}"
WAD="${2:-}"
PIN=e257a09527062bfb986b6e5e0780839b6cbd5b71
[[ -d "$UP" && -f "$WAD" ]] || { echo "usage: bash $0 /path/to/pinned/doomgeneric /path/to/freedoom1.wad" >&2; exit 2; }
[[ "$(git -C "$UP" rev-parse HEAD)" == "$PIN" ]] || { echo "upstream revision mismatch" >&2; exit 1; }
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
python3 "$ROOT/tools/doom-upstream-compile/inventory.py" --source "$UP" --output "$OUT/inventory"
python3 - "$OUT/inventory/inventory.json" "$OUT/objects.txt" <<'PY'
import json,pathlib,sys
obj=[x["object"] for x in json.loads(pathlib.Path(sys.argv[1]).read_text())["files"]]
if len(obj)<70:raise SystemExit("refusing incomplete engine object set")
pathlib.Path(sys.argv[2]).write_text("\n".join(obj)+"\n")
PY
mapfile -t OBJECTS < "$OUT/objects.txt"
cc -std=gnu11 -O2 -Wall -Wextra -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
    -I"$UP" "$ROOT/tools/doom-real-freedoom-host/main.c" "${OBJECTS[@]}" -lm \
    -o "$OUT/doom-real-freedoom-host"
timeout 90 "$OUT/doom-real-freedoom-host" "$(realpath "$WAD")"
