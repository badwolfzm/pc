#!/usr/bin/env python3
"""Genuine Warden+Doom ET_REL one-import diagnostic, *not* an executable verdict."""
import json
import pathlib
import subprocess
import sys

TARGET = 'strtod'
PROTECTED = frozenset({'system', 'fopen', 'fclose', 'fread', 'fwrite', 'fseek',
                       'ftell', 'remove', 'rename'})


def unresolved(path):
    out = subprocess.check_output(['nm', '-P', '-g', '-u', str(path)], text=True)
    found = set()
    for line in out.splitlines():
        items = line.split()
        if len(items) < 2 or items[1] != 'U' or items[0] in found:
            raise ValueError('unexpected nm output: ' + line)
        found.add(items[0])
    return found


def verify(before, after):
    if TARGET not in before:
        raise ValueError('strtod import absent in baseline')
    if after != before - {TARGET}:
        raise ValueError('provider masked other imports or added imports: '
                         + str(sorted(before ^ after)))
    if not PROTECTED.issubset(after):
        raise ValueError('host file and process calls unexpectedly resolved')
    if '__errno_location' not in after:
        raise ValueError('errno provider must remain a guest-runtime dependency')
    return {
        'resolved': [TARGET],
        'previously_missing_count': len(before),
        'still_missing_count': len(after),
        'remaining_undefined': sorted(after),
        'real_engine_partial_link': True,
        'native_warden_game_boot': False,
        'guest_memory_measured': False,
        'fps_measured': False,
        'correctly_rounded_strtod': False,
        'ready_for_runtime_integration': False,
        'warning': 'Diagnostic-only approximation: known last-bit decimal differences',
    }


def main(before, after, output):
    result = verify(unresolved(before), unresolved(after))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: verify.py before.o after.o report.json')
    main(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]))
