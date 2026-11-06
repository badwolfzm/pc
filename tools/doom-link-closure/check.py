#!/usr/bin/env python3
"""Name-only Doom engine link-gap report; not a real linker or boot proof."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

SYMBOL = re.compile(r"^[A-Za-z_.$][A-Za-z0-9_.$@]*$")
MAX_INVENTORY = 2 * 1024 * 1024
MAX_OBJECTS = 64
FORBIDDEN = {'system', 'popen', 'fork', 'execve', 'dlopen', 'dlsym'}

def inventory_symbols(report):
    if not isinstance(report, dict) or not isinstance(report.get('unresolved_external_symbols'), list):
        raise ValueError('invalid inventory schema')
    symbols = report['unresolved_external_symbols']
    if len(symbols) > 8192 or any(not isinstance(s, str) or not SYMBOL.fullmatch(s) for s in symbols):
        raise ValueError('malformed or excessive unresolved symbols')
    if len(set(symbols)) != len(symbols):
        raise ValueError('duplicate inventory symbols')
    return set(symbols)

def parse_nm(content):
    definitions = set()
    for line in content.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] in ('T', 'D', 'B', 'R', 'A', 'C', 'W', 'V') and SYMBOL.fullmatch(parts[0]):
            definitions.add(parts[0])
    return definitions

def compare(required, supplied):
    matched = sorted(required & supplied)
    missing = sorted(required - supplied)
    return {
        'total_unresolved_in_upstream_host_inventory': len(required),
        'matched_by_candidate_object_definitions': len(matched),
        'matched_symbols': matched,
        'still_missing': missing,
        'unsafe_host_services_require_explicit_policy': sorted(required & FORBIDDEN),
        'complete': not missing,
        'scope': 'name-only preflight; no signature/ABI compatibility, native link, game execution, RAM or FPS proof'
    }

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory', type=Path)
    parser.add_argument('objects', nargs='+', type=Path)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args(argv)
    try:
        if len(args.objects) > MAX_OBJECTS or args.inventory.stat().st_size > MAX_INVENTORY:
            raise ValueError('input exceeds budget')
        required = inventory_symbols(json.loads(args.inventory.read_text()))
        for obj in args.objects:
            if not obj.is_file() or obj.stat().st_size > 32 * 1024 * 1024:
                raise ValueError('invalid or oversized object')
        run = subprocess.run(['nm', '-P', '-g', '--defined-only', *map(str, args.objects)],
                             capture_output=True, text=True, timeout=15, check=False)
        if run.returncode:
            raise ValueError('nm failed: ' + run.stderr[:300])
        result = compare(required, parse_nm(run.stdout))
        print(json.dumps(result, indent=2, sort_keys=True))
        return 1 if args.strict and not result['complete'] else 0
    except (OSError, ValueError, json.JSONDecodeError, subprocess.TimeoutExpired) as exc:
        print(json.dumps({'status': 'FAIL', 'complete': False, 'error': str(exc)}), file=sys.stderr)
        return 2

if __name__ == '__main__':
    sys.exit(main())
