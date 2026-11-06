#!/usr/bin/env python3
"""Require exactly one genuine Doom unresolved import to disappear."""
import json
import pathlib
import subprocess
import sys

TARGET = '__ctype_toupper_loc'
PROTECTED = frozenset({'system', 'fopen', 'fread', 'fwrite', 'remove', 'rename'})


def undefined(path):
    out = subprocess.check_output(['nm', '-P', '-g', '-u', str(path)], text=True)
    result = set()
    for line in out.splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[1] != 'U' or fields[0] in result:
            raise ValueError('unexpected nm output: ' + line)
        result.add(fields[0])
    return result


def check(old, new):
    if TARGET not in old:
        raise ValueError('target symbol absent before link')
    if new != old - {TARGET}:
        raise ValueError('unexpected symbol removal/addition: ' + str(sorted(old ^ new)))
    if not PROTECTED.issubset(new):
        raise ValueError('protected external host operations disappeared')
    return {'resolved_symbols': [TARGET], 'original_unresolved_count': len(old),
            'remaining_unresolved_count': len(new),
            'remaining_symbols': sorted(new), 'real_upstream_engine_compiled': True,
            'partial_link_only': True, 'warden_native_boot_verified': False,
            'game_fps_measured': False, 'guest_ram_measured': False,
            'limitations': 'C locale only, no dynamic locale/TLS; glibc ABI diagnostic only'}


def run(before, after, report):
    result = check(undefined(before), undefined(after))
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit('usage: verify.py before.o after.o report.json')
    run(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]))
