# Warden browser console — experimental host bridge

**Status:** Host-side prototype on its own branch. Does **not** boot the Warden kernel or display live Doom by itself. An optional QMP helper can capture an existing local QEMU VM. The browser can show a synthetic source, or QEMU screenshots using the optional trusted QMP helper; no native guest-to-web rendering path is claimed.

## Run locally (Node.js 18+)

Use a long password and a distinct high-entropy ingest token. Do not commit either value.

```sh
cd tools/warden-web-console
export WARDEN_WEB_PASSWORD='replace-with-a-long-secret-password'
export WARDEN_INGEST_TOKEN="$(openssl rand -hex 32)"
node server.js
# In another terminal, with the same WARDEN_INGEST_TOKEN:
node demo.js
```

Open **http://localhost:8787/** in the same machine's browser and enter the configured password. The demo paints moving colored pixels (10 frames/s). Keyboard/touch inputs are returned to the demo via the authorized input polling endpoint. To run automated checks: `node tools/warden-web-console/test.js` from the repository root.

For phone/tablet access, keep the server bound to loopback and place it behind a TLS reverse proxy or a secured VPN tunnel. Set `WARDEN_WEB_ORIGIN` to the exact public HTTPS origin of that proxy (including port if nonstandard). Use WSS over the public connection. Do **not** bind this prototype directly to the public internet. Authentication/session limits are deliberately modest and have not received an independent security audit.

## One-terminal WSL test of a real QEMU framebuffer

Instead of managing separate server passwords, ingest tokens, QMP sockets and
three concurrent terminals, use the optional launcher. It generates one shared
ephemeral token and a private temporary QMP socket for the two child processes;
it does **not** use `demo.js` or the rainbow generator.

```sh
# Terminal: WSL Ubuntu, NOT Windows cmd.exe.
cd ~/warden/pc-web-console
bash tools/warden-web-console/run_with_qemu.sh \
  "$HOME/warden/pc-game-test/target/warden-game.mb32"
```

Enter a new password (12+ characters), then visit **http://localhost:8787/**
from your PC browser. The launcher stops its server and QEMU children on exit;
a preexisting process bound to port 8787 is reported rather than killed.
Use Ctrl+C to stop. Do not concurrently run the old rainbow demo.
If the kernel image is missing, first build the G0 game probe in its own
worktree. Passing the G0 image launches a synthetic Warden graphics test,
**not actual Doom**; the probe finishes after 60 frames, so display may
become static or QEMU may exit quickly. This command has not been tested
end-to-end with a Warden guest on the user's computer.

## Optional live QEMU screen relay (experimental)

The host-side `qmp_bridge.py` can screenshot an **existing** QEMU VM through a **user-owned local UNIX QMP socket** and push frames into the browser console; it relays an allow-list of keyboard keys back through QMP. No Warden kernel modifications or untrusted access to QMP are required. Example:

```sh
# Terminal A: start Warden or a long-running QEMU guest with:
# qemu-system-x86_64 ... -display none -vga std \
#   -qmp unix:$HOME/warden/warden-qmp.sock,server=on,wait=off

# Terminal B: start web server, with both WARDEN_* secrets exported
node tools/warden-web-console/server.js

# Terminal C: use the SAME WARDEN_INGEST_TOKEN:
python3 tools/warden-web-console/qmp_bridge.py \
  --qmp-socket "$HOME/warden/warden-qmp.sock" --fps 5
```

The existing G0 QEMU probe automatically exits after its 60 verified frames; it is **not a continuous game session**, so its screen will be short-lived. For an actual sustained interactive Warden display, a long-running guest and end-to-end QMP/device tests are still necessary. Captures are PPM screenshots resized to 320×200 (nearest-neighbor); this is a polling bridge with CPU and I/O overhead, not a zero-copy graphical backend. Mouse injection is not yet implemented; the Canvas pointer interface is defined but ignored by this QMP bridge.

Never expose the QMP socket over TCP or to a remote browser. QMP is a highly privileged hypervisor control interface. Only this trusted local helper may connect to it. Browser viewers only receive RGBA frames and can send allow-listed input events.

## Wire protocol (for a future guest-to-host transport)

- `POST /api/login`: JSON `{"password":"..."}` from exactly configured Origin. Returns a **20-minute, single-use** 256-bit hexadecimal token for WebSocket login; five failed attempts per source IP per ten minutes (process-local rate limit).
- `GET /ws`: Origin must match `WARDEN_WEB_ORIGIN`; use WebSocket subprotocols `warden.v1` and `warden.auth.<token>`. Token is consumed upon upgrade; TLS/WSS strongly recommended. At most four viewers per process. Server pushes **binary** raw RGBA8888, 320×200, 256,000 bytes/frame; alpha ignored by Canvas. No frame compression or GPU acceleration claims.
- `POST /api/frame`: application/octet-stream, **exactly 256,000 bytes**, authenticated with `X-Warden-Ingest-Token`. Only a separately authorized local frame producer may call it; upload broadcasts to active clients.
- `GET /api/input`: authenticated with the same ingest secret, consumes a queue of at most 256 validated input events. `{"kind":"key","code":"ArrowUp","down":true}` or `{"kind":"pointer","x":10,"y":20,"buttons":1}`. This is a proof-of-interface: it does not yet marshal input to Warden's kernel or PS/2 implementation.

The host bridge must not hold guest-exit, privileged device or general filesystem capabilities. A future producer should copy only a bounded display buffer from a reviewed Warden display endpoint; input must traverse a separately granted guest input service, with no arbitrary syscalls or host command execution. Keep viewer and frame-producer credentials distinct. One viewer can control the demo; multi-viewer input arbitration has not yet been implemented. It is not ready for untrusted internet exposure.

## Tests / limits

```sh
node tools/warden-web-console/test.js
```

Uses a locally spawned server and loopback requests; verifies password rejection, Origin enforcement, token consumption/replay rejection, ingest authorization, frame length limit, WebSocket handshake and input polling. Does **not** prove WebSocket frame rendering fidelity in a browser, rate limiting under load, resistance to all hostile traffic, Warden QEMU integration, real Doom, FPS, or memory budgets. Review and run on the user's Windows/WSL machine before merging.

Owned paths: `tools/warden-web-console/**`, `.github/workflows/warden-web-console.yml`. Coordinates with the G0 renderer and Doom G1 issues, without modifying kernel or game files. Keep this PR in draft until independent review and CI verify exact head.
