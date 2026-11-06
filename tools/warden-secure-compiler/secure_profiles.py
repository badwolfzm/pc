#!/usr/bin/env python3
"""Non-executing, bounded MatrixOS/FastStripCC-inspired compiler plan emitter."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

MAX_SOURCE = 2 * 1024 * 1024
MAX_MANIFEST = 65536
PROFILES = {
    'matrixos': 'matrixos',
    'runtime-fast': 'runtime-fast',
    'size': 'matrixos',  # sections+gc; NOT a real FastStrip optimizer
}
TIERS = ('baseline', 'avx2', 'avx512')

class Refused(ValueError):
    pass

def bounded_regular_source(root: Path, name: str):
    if not isinstance(name, str) or not name or len(name) > 240 or '\x00' in name:
        raise Refused('invalid source path')
    p = Path(name)
    if p.is_absolute() or any(part in ('..', '.') for part in p.parts) or p.suffix != '.c':
        raise Refused('only relative C source paths inside trusted root')
    if p.parts[0].startswith('-') or any(part.startswith('-') for part in p.parts):
        raise Refused('leading option-like path segment')
    at = root
    for part in p.parts:
        at = at / part
        if at.is_symlink():
            raise Refused('symlink refused')
    resolved = at.resolve(strict=True)
    try:
        resolved.relative_to(root)
    except ValueError as exc:
        raise Refused('path escapes trusted root') from exc
    st = resolved.stat()
    if not stat.S_ISREG(st.st_mode) or st.st_size > MAX_SOURCE or st.st_size == 0:
        raise Refused('source must be bounded nonempty regular file')
    fd = os.open(resolved, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    try:
        fst = os.fstat(fd)
        if (fst.st_dev, fst.st_ino) != (st.st_dev, st.st_ino) or fst.st_size != st.st_size:
            raise Refused('source changed during inspection')
        raw = os.read(fd, MAX_SOURCE + 1)
        if len(raw) != st.st_size:
            raise Refused('source changed while reading')
    finally:
        os.close(fd)
    return {'path': p.as_posix(), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}

def plan(root: Path, source: str, profile: str, tier: str):
    if profile == 'faststripcc':
        raise Refused('FastStripCC is not present as a reviewed repository compiler; no external wrapper execution permitted')
    if profile not in PROFILES or tier not in TIERS:
        raise Refused('unknown profile or architecture tier')
    if root.is_symlink() or not root.is_dir():
        raise Refused('trusted root is not a directory')
    root = root.resolve(strict=True)
    item = bounded_regular_source(root, source)
    return {
        'format': 'warden-optimizer-plan-v1',
        'mode': 'plan-only',
        'executes_compiler': False,
        'runs_game_binary': False,
        'source': item,
        'profile': profile,
        'compiler_profile': PROFILES[profile],
        'tier': tier,
        'compiler_driver': 'matrixos/frontend/build/mxcc',
        'intended_options': ['--profile='+PROFILES[profile], '--tier='+tier, '--strict'],
        'faststripcc_integration': 'not-linked; independent source and security review required',
        'warnings': [
            'This is metadata, not a compile or sandbox security boundary.',
            'A trusted offline build runner must enforce filesystem isolation, no network, privilege dropping, syscall controls, resource caps and artifact review.',
            'Do not send untrusted source to a live game server or execute outputs automatically.',
        ],
    }

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trusted-source-root', type=Path, required=True)
    ap.add_argument('--source', required=True)
    ap.add_argument('--profile', choices=(*PROFILES, 'faststripcc'), default='matrixos')
    ap.add_argument('--tier', choices=TIERS, default='baseline')
    args = ap.parse_args(argv)
    try:
        result = plan(args.trusted_source_root, args.source, args.profile, args.tier)
        blob = json.dumps(result, indent=2, sort_keys=True)
        if len(blob.encode('utf-8')) > MAX_MANIFEST:
            raise Refused('plan exceeds output limit')
        print(blob)
        return 0
    except (Refused, OSError) as exc:
        print('REFUSED: '+str(exc), file=sys.stderr)
        return 2

if __name__ == '__main__':
    sys.exit(main())
