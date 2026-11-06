#!/usr/bin/env python3
"""Optional QEMU/QMP screenshot + allow-listed keyboard relay to Warden Canvas.
Runs on the trusted host, not inside Warden. Never exposes QMP to browsers.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import stat
import tempfile
import time
import urllib.request

W, H = 320, 200
MAX_SOURCE = 4096
KEYS = {'ArrowUp': 'up', 'ArrowDown': 'down', 'ArrowLeft': 'left',
        'ArrowRight': 'right', 'Space': 'spc', 'Enter': 'ret', 'Escape': 'esc'}
KEYS.update({f'Key{c}': c.lower() for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'})
KEYS.update({f'Digit{c}': c for c in '0123456789'})


def ppm_to_rgba(p):
    """Parse bounded binary P6 screenshot and scale nearest-neighbor to 320x200."""
    if len(p) > MAX_SOURCE * MAX_SOURCE * 3 + 8192:
        raise ValueError('screenshot exceeds maximum size')
    i = 0
    tokens = []
    while len(tokens) < 4:
        while i < len(p) and p[i] in b' \t\r\n':
            i += 1
        if i < len(p) and p[i] == 35:
            end = p.find(b'\n', i)
            if end < 0:
                raise ValueError('unterminated PPM comment')
            i = end + 1
            continue
        start = i
        while i < len(p) and p[i] not in b' \t\r\n#':
            i += 1
        if i == start or i-start > 16:
            raise ValueError('invalid PPM header')
        tokens.append(p[start:i])
    if tokens[0] != b'P6' or tokens[3] != b'255':
        raise ValueError('only P6 maxval 255 supported')
    sw, sh = int(tokens[1]), int(tokens[2])
    if not (1 <= sw <= MAX_SOURCE and 1 <= sh <= MAX_SOURCE):
        raise ValueError('invalid dimensions')
    if i >= len(p) or p[i] not in b' \t\r\n':
        raise ValueError('missing header delimiter')
    # P6 raster follows a single whitespace separator, even if first RGB byte is whitespace.
    i += 1
    rgb = memoryview(p)[i:]
    if len(rgb) != sw*sh*3:
        raise ValueError('incorrect PPM raster length')
    out = bytearray(W*H*4)
    for y in range(H):
        sy = (y*sh//H)*sw*3
        for x in range(W):
            src = sy + (x*sw//W)*3
            dst = (y*W+x)*4
            out[dst:dst+3] = rgb[src:src+3]
            out[dst+3] = 255
    return bytes(out)


class QMP:
    def __init__(self, path):
        p = Path(path)
        if not p.is_socket() or p.stat().st_uid != os.getuid():
            raise ValueError('QMP socket must be a UNIX socket owned by this user')
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect(path)
        self.reader = self.sock.makefile('rb')
        self.read_message()
        self.execute('qmp_capabilities')

    def read_message(self):
        for _ in range(100):
            line = self.reader.readline(1024*1024)
            if not line:
                raise ConnectionError('QMP disconnected')
            msg = json.loads(line)
            if 'event' not in msg:
                return msg
        raise RuntimeError('too many QMP events')

    def execute(self, command, args=None):
        req = {'execute': command}
        if args is not None:
            req['arguments'] = args
        self.sock.sendall(json.dumps(req).encode() + b'\r\n')
        result = self.read_message()
        if 'error' in result:
            raise RuntimeError('QMP rejected ' + command + ': ' + str(result['error']))
        if 'return' not in result:
            raise RuntimeError('unexpected QMP response')
        return result['return']

    def key(self, code, down):
        qcode = KEYS.get(code)
        if not qcode or type(down) is not bool:
            return
        self.execute('input-send-event', {'events': [
            {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': qcode}}}
        ]})

    def screenshot(self, filename):
        self.execute('screendump', {'filename': filename})
        return ppm_to_rgba(Path(filename).read_bytes())

    def close(self):
        self.reader.close()
        self.sock.close()


def api(host, port, secret, path, data=None):
    url = f'http://{host}:{port}{path}'
    headers = {'X-Warden-Ingest-Token': secret}
    if data is not None:
        headers['Content-Type'] = 'application/octet-stream'
    req = urllib.request.Request(url, data=data, headers=headers,
                                 method='POST' if data is not None else 'GET')
    with urllib.request.urlopen(req, timeout=3) as response:
        return response.read()


def run(args):
    secret = os.environ.get('WARDEN_INGEST_TOKEN', '')
    if len(secret) < 24:
        raise ValueError('WARDEN_INGEST_TOKEN must be 24+ characters')
    qmp = QMP(args.qmp_socket)
    down = {}
    try:
        with tempfile.TemporaryDirectory(prefix='warden-qmp-') as tmp:
            filename = str(Path(tmp) / 'frame.ppm')
            interval = 1 / args.fps
            while True:
                start = time.monotonic()
                frame = qmp.screenshot(filename)
                api(args.host, args.port, secret, '/api/frame', frame)
                reply = json.loads(api(args.host, args.port, secret, '/api/input'))
                for event in reply.get('events', [])[:256]:
                    if event.get('kind') != 'key':
                        continue  # Mouse/absolute pointer injection is not yet integrated.
                    code = event.get('code')
                    if code not in KEYS or type(event.get('down')) is not bool:
                        continue
                    qmp.key(code, event['down'])
                    if event['down']:
                        down[code] = time.monotonic()
                    else:
                        down.pop(code, None)
                for code, since in list(down.items()):
                    if time.monotonic() - since > 2.0:
                        qmp.key(code, False)
                        down.pop(code, None)
                time.sleep(max(0, interval - (time.monotonic()-start)))
    finally:
        for code in down:
            try:
                qmp.key(code, False)
            except Exception:
                pass
        qmp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qmp-socket', required=True)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--fps', type=int, default=5, choices=range(1, 11), metavar='1..10')
    options = parser.parse_args()
    try:
        run(options)
    except KeyboardInterrupt:
        print('QMP console stopped')
