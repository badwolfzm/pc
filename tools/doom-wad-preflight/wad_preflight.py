#!/usr/bin/env python3
"""Bounded, read-only preflight for classic Doom IWAD/PWAD directory metadata.

This is a host-side artifact inspector, not a Warden kernel loader or game runtime.
"""
import argparse
import json
import os
import struct
import sys

MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_LUMPS = 65536
ENTRY_BYTES = 16
HEADER_BYTES = 12


class WadError(ValueError):
    pass


def inspect_wad(path):
    with open(path, 'rb') as f:
        length = os.fstat(f.fileno()).st_size
        if length > MAX_FILE_BYTES:
            raise WadError('file exceeds 64 MiB safety limit')
        if length < HEADER_BYTES:
            raise WadError('truncated WAD header')
        header = f.read(HEADER_BYTES)
        magic, count, directory = struct.unpack('<4sII', header)
        if magic not in (b'IWAD', b'PWAD'):
            raise WadError('invalid WAD signature')
        if count > MAX_LUMPS:
            raise WadError('lump count exceeds safety limit')
        size = count * ENTRY_BYTES
        if directory > length or size > length - directory:
            raise WadError('directory lies outside file')
        f.seek(directory)
        names = []
        maps = []
        for i in range(count):
            raw = f.read(ENTRY_BYTES)
            if len(raw) != ENTRY_BYTES:
                raise WadError('truncated directory entry')
            offset, nbytes, name = struct.unpack('<II8s', raw)
            if offset > length or nbytes > length - offset:
                raise WadError(f'lump {i} lies outside file')
            clean = name.split(b'\0', 1)[0]
            if not clean or any(c < 32 or c > 126 for c in clean):
                raise WadError(f'lump {i} has an invalid name')
            label = clean.decode('ascii')
            names.append(label)
            if ((len(label) == 4 and label.startswith('E') and label[2] == 'M'
                 and label[1].isdigit() and label[3].isdigit()) or
                (len(label) == 5 and label.startswith('MAP') and label[3:].isdigit())):
                maps.append(label)
        return {'format': magic.decode('ascii'), 'bytes': length,
                'lumps': count, 'map_markers': maps,
                'has_playpal': 'PLAYPAL' in names,
                'has_colormap': 'COLORMAP' in names}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('wad', help='path to legally obtained Doom-format IWAD/PWAD')
    args = ap.parse_args(argv)
    try:
        report = inspect_wad(args.wad)
    except (OSError, WadError) as exc:
        print(f'WAD preflight failed: {exc}', file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
