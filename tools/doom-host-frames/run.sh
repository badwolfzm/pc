#!/usr/bin/env bash
# G1 host game-loop smoke: actual Doom engine with original Warden callbacks.
# Synthetic host Warden syscalls and host libc. DO NOT treat as Warden QEMU boot.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UP="${1:?path/to/pinned/upstream/doomgeneric}"
WAD="${2:?path/to/licensed/DOOM1.WAD}"
[[ "$(git -C "$UP" rev-parse HEAD)" == "e257a09527062bfb986b6e5e0780839b6cbd5b71" ]]
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
python3 "$ROOT/tools/doom-upstream-compile/inventory.py" --source "$UP" --output "$OUT/i"
python3 - "$OUT/i/inventory.json" "$OUT/objects.txt" <<'PY'
import json,pathlib,sys
d=json.loads(pathlib.Path(sys.argv[1]).read_text())
o=[x["object"] for x in d["files"] if not x["object"].endswith("/w_file_stdc.o")]
assert len(o)>=70
pathlib.Path(sys.argv[2]).write_text("\n".join(o)+"\n")
PY
mapfile -t OBJECTS < "$OUT/objects.txt"
FLAGS=(-std=gnu11 -O2 -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
       -I"$UP" -I"$ROOT/tools/doom-wad-stream" -I"$ROOT/tools/doom-wad-class-bridge")
cc "${FLAGS[@]}" -c "$ROOT/tools/doom-wad-stream/wad_stream.c" -o "$OUT/stream.o"
cc "${FLAGS[@]}" -c "$ROOT/tools/doom-wad-class-bridge/doom_wad_bridge.c" -o "$OUT/bridge.o"
cc "${FLAGS[@]}" -c "$ROOT/tools/doom-real-wfile-link/bridge_backend.c" -o "$OUT/backend.o"
cc "${FLAGS[@]}" -DWARDEN_DOOM_TEST -c "$ROOT/tools/doomgeneric-warden/doomgeneric_warden.c" -o "$OUT/display.o"
cc "${FLAGS[@]}" -c "$ROOT/tools/doom-host-frames/game_smoke.c" -o "$OUT/game.o"
cc "${OBJECTS[@]}" "$OUT/stream.o" "$OUT/bridge.o" "$OUT/backend.o" "$OUT/display.o" "$OUT/game.o" -lm -Wl,--wrap=DG_GetKey -o "$OUT/doom-host-frames"
# Last resort timeout as Doom may hang during initialization; no QEMU involved.
EVIDENCE_DIR="${DOOM_EVIDENCE_DIR:-$OUT/evidence}"
mkdir -p "$EVIDENCE_DIR"
echo "Doom host control: zero input, genuine level simulation"
timeout 30s "$OUT/doom-host-frames" "$WAD" --no-input | tee "$EVIDENCE_DIR/control.log"
echo "Doom host intervention: up-arrow press/release through merged Warden adapter"
timeout 30s "$OUT/doom-host-frames" "$WAD" | tee "$EVIDENCE_DIR/intervention.log"
echo "Doom host reverse intervention: down-arrow press/release through merged Warden adapter"
timeout 30s "$OUT/doom-host-frames" "$WAD" --reverse-input | tee "$EVIDENCE_DIR/reverse.log"
python3 "$ROOT/tools/doom-host-frames/check_evidence.py" \
  "$EVIDENCE_DIR/control.log" "$EVIDENCE_DIR/intervention.log" \
  "$EVIDENCE_DIR/controlled-input.json"
