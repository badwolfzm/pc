#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UP="${1:-}"; WAD="${2:-}"; DEST="${3:-}"
PIN=e257a09527062bfb986b6e5e0780839b6cbd5b71
WAD_SHA=7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d
[[ -d "$UP" && -f "$WAD" && -n "$DEST" ]] || { echo "usage: bash $0 pinned/doomgeneric freedoom1.wad output-dir" >&2; exit 2; }
[[ "$(git -C "$UP" rev-parse HEAD)" == "$PIN" ]] || { echo "unapproved Doom source revision" >&2; exit 1; }
echo "$WAD_SHA  $WAD" | sha256sum -c -
mkdir -p "$DEST"
DEST="$(realpath "$DEST")"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
python3 "$ROOT/tools/doom-upstream-compile/inventory.py" --source "$UP" --output "$BUILD/inventory"
python3 - "$BUILD/inventory/inventory.json" "$BUILD/objs.txt" <<'PY'
import json,pathlib,sys
o=[i["object"] for i in json.loads(pathlib.Path(sys.argv[1]).read_text())["files"]]
if len(o)!=80:raise SystemExit("unexpected upstream engine object count")
pathlib.Path(sys.argv[2]).write_text("\n".join(o)+"\n")
PY
mapfile -t OBJECTS < "$BUILD/objs.txt"
cc -std=gnu11 -O2 -Wall -Wextra -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
   -I"$UP" "$ROOT/tools/doom-300-frame-host-evidence/main.c" \
   "${OBJECTS[@]}" -lm -o "$BUILD/doom-frame-host"
CPU="$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')"
timeout 45 taskset -c "$CPU" "$BUILD/doom-frame-host" "$(realpath "$WAD")" > "$DEST/doom-frames.log"
python3 "$ROOT/tools/doom-300-frame-host-evidence/verify.py" \
   "$DEST/doom-frames.log" --required 300 --output "$DEST/frame-evidence.json"
echo "Verified pinned upstream Doom/FreeDoom host CPU $CPU, 300 frame callbacks. NOT Warden."
