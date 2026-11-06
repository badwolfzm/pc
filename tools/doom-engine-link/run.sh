#!/usr/bin/env bash
# Host link check for REAL pinned upstream Doom translation units.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PIN=e257a09527062bfb986b6e5e0780839b6cbd5b71
UP="${1:-}"
[[ -n "$UP" && -d "$UP" ]] || { echo "usage: bash $0 /path/to/doomgeneric/subdir" >&2; exit 2; }
[[ "$(git -C "$UP" rev-parse HEAD)" == "$PIN" ]] || { echo "upstream SHA mismatch" >&2; exit 1; }
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
python3 "$ROOT/tools/doom-upstream-compile/inventory.py" --source "$UP" --output "$OUT/inventory"
# Replace the upstream stdio backend with capability-scoped Warden backend.
# Use real, pinned upstream headers in every adapter translation unit.
python3 - "$OUT/inventory/inventory.json" "$OUT/objects.txt" <<'PY'
import json, pathlib, sys
report = json.loads(pathlib.Path(sys.argv[1]).read_text())
files = [f["object"] for f in report["files"] if pathlib.Path(f["object"]).name != "w_file_stdc.o"]
if not files or len(files) < 70: raise SystemExit("incomplete genuine upstream object set")
pathlib.Path(sys.argv[2]).write_text("\n".join(files)+"\n")
PY
mapfile -t OBJECTS < "$OUT/objects.txt"
COMMON=(-std=gnu11 -O2 -Wall -Wextra -Wno-error=sign-compare
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
    -I"$UP" -I"$ROOT/tools/doom-wad-stream"
    -I"$ROOT/tools/doom-wad-class-bridge")
cc "${COMMON[@]}" -c "$ROOT/tools/doom-wad-stream/wad_stream.c" -o "$OUT/stream.o"
cc "${COMMON[@]}" -c "$ROOT/tools/doom-wad-class-bridge/doom_wad_bridge.c" -o "$OUT/bridge.o"
cc "${COMMON[@]}" -c "$ROOT/tools/doom-real-wfile-link/bridge_backend.c" -o "$OUT/backend.o"
# Native Warden display adapter tested on host with syscall mocks.
cc "${COMMON[@]}" -DWARDEN_DOOM_TEST -c "$ROOT/tools/doomgeneric-warden/doomgeneric_warden.c" -o "$OUT/display.o"
cc "${COMMON[@]}" -c "$ROOT/tools/doom-engine-link/smoke.c" -o "$OUT/smoke.o"
cc "${OBJECTS[@]}" "$OUT/stream.o" "$OUT/bridge.o" "$OUT/backend.o" "$OUT/display.o" "$OUT/smoke.o" -lm -o "$OUT/engine-link-smoke"
"$OUT/engine-link-smoke"
echo "PASS: genuine pinned upstream Doom objects link into one host executable against Warden adapters"
