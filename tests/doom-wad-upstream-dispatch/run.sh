#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SOURCE="${1:?provide path to checked-out pinned doomgeneric/ directory}"
ACTUAL="$(git -C "$SOURCE" rev-parse HEAD)"
PIN=e257a09527062bfb986b6e5e0780839b6cbd5b71
[[ "$ACTUAL" == "$PIN" ]] || { echo "upstream revision mismatch" >&2; exit 2; }
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
# Compile the *actual* upstream W_OpenFile implementation and public w_file.h,
# but do not compile its stdio backend. Link explicit Warden-authorized shim.
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
 -I"$SOURCE" -I"$ROOT/tools/doom-wad-class-bridge" -I"$ROOT/tools/doom-wad-stream" \
 "$SOURCE/w_file.c" \
 "$ROOT/tools/doom-wad-class-bridge/doom_wad_bridge.c" \
 "$ROOT/tools/doom-wad-stream/wad_stream.c" \
 "$ROOT/tests/doom-wad-upstream-dispatch/test_dispatch.c" -o "$OUT/dispatch"
"$OUT/dispatch"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
 -I"$SOURCE" -I"$ROOT/tools/doom-wad-class-bridge" -I"$ROOT/tools/doom-wad-stream" \
 "$SOURCE/w_file.c" \
 "$ROOT/tools/doom-wad-class-bridge/doom_wad_bridge.c" \
 "$ROOT/tools/doom-wad-stream/wad_stream.c" \
 "$ROOT/tests/doom-wad-upstream-dispatch/test_dispatch.c" -o "$OUT/dispatch-ubsan"
"$OUT/dispatch-ubsan"
