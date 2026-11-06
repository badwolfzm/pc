#!/usr/bin/env python3
"""Fail-closed ELF64 Warden native-game memory map preflight; not a boot test."""
import argparse
import json
import pathlib
import re
import subprocess
import sys

CODE_LO, DATA_LO, DATA_HI, STACK_LO = 0x800000, 0x840000, 0x9e0000, 0x9f0000
STACK_HI = 0xa00000
MAX_FILE = 32 * 1024 * 1024

def parse_readelf(output):
    match = re.search(r"Entry point(?: address)?:?\s*(0x[0-9a-fA-F]+)", output)
    if not match:
        raise ValueError("missing ELF entry point")
    entry = int(match.group(1), 16)
    loads, forbidden = [], []
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        kind = fields[0]
        if kind in ("INTERP", "DYNAMIC", "TLS") or (kind == "GNU_STACK" and "E" in fields[1:]):
            forbidden.append(kind)
        if kind != "LOAD":
            continue
        if len(fields) < 8:
            raise ValueError("unrecognized LOAD program header")
        try:
            offset, vaddr, file_size, mem_size = (int(fields[i], 16) for i in (1, 2, 4, 5))
        except ValueError as exc:
            raise ValueError("invalid LOAD numeric field") from exc
        flags = ''.join(fields[6:-1])
        if not flags or any(c not in 'RWE' for c in flags):
            raise ValueError("invalid LOAD flags")
        loads.append(dict(offset=offset, vaddr=vaddr, file_size=file_size, mem_size=mem_size, flags=flags))
    return entry, loads, forbidden

def validate(entry, loads, forbidden):
    errors = []
    if forbidden:
        errors.append("unsupported program headers: " + ', '.join(sorted(set(forbidden))))
    if not loads:
        errors.append("no PT_LOAD segments")
    intervals = []
    valid_entry = False
    for i, seg in enumerate(loads):
        start, mem, fs = seg['vaddr'], seg['mem_size'], seg['file_size']
        flags = seg['flags']
        end = start + mem
        if mem == 0 or fs > mem or end > 0xffffffffffffffff or end <= start:
            errors.append(f"LOAD {i}: empty/invalid size or address overflow")
            continue
        if 'W' in flags and 'E' in flags:
            errors.append(f"LOAD {i}: writable and executable (W^X violation)")
        if 'E' in flags:
            if not (CODE_LO <= start < end <= DATA_LO):
                errors.append(f"LOAD {i}: executable segment outside code window")
            if start <= entry < end:
                valid_entry = True
        elif 'W' in flags:
            if not (DATA_LO <= start < end <= DATA_HI):
                errors.append(f"LOAD {i}: writable segment outside data window")
        elif not (CODE_LO <= start < end <= DATA_HI):
            errors.append(f"LOAD {i}: read-only segment outside guest windows")
        if start < STACK_HI and end > STACK_LO:
            errors.append(f"LOAD {i}: overlaps reserved guest stack")
        intervals.append((start, end, i))
    sorted_intervals = sorted(intervals)
    for a, b in zip(sorted_intervals, sorted_intervals[1:]):
        if a[1] > b[0]:
            errors.append(f"LOAD {a[2]} and LOAD {b[2]}: overlapping virtual ranges")
    if not valid_entry:
        errors.append("entry point not inside an executable PT_LOAD")
    return errors

def audit(path):
    file = pathlib.Path(path)
    if not file.is_file() or file.stat().st_size > MAX_FILE or file.stat().st_size == 0:
        raise ValueError("ELF is missing, empty, or larger than 32 MiB")
    header = subprocess.run(['readelf', '-hW', str(file)], capture_output=True, text=True,
                            timeout=10, check=False)
    if (header.returncode != 0 or
        not re.search(r'Class:\s*ELF64\b', header.stdout) or
        not re.search(r'Machine:\s*Advanced Micro Devices X86-64\b', header.stdout) or
        not re.search(r'Type:\s*EXEC\b', header.stdout)):
        raise ValueError('expected x86-64 ELF64 ET_EXEC image (not PIE, shared object or wrong architecture)')
    run = subprocess.run(['readelf', '-lW', str(file)], capture_output=True, text=True,
                         timeout=10, check=False)
    if run.returncode != 0:
        raise ValueError("readelf rejected input: " + run.stderr.strip()[:200])
    entry, loads, forbidden = parse_readelf(run.stdout)
    errors = validate(entry, loads, forbidden)
    return dict(status='FAIL' if errors else 'PASS', entry=f'0x{entry:x}',
                segments=loads, errors=errors,
                note='static layout only: not a Warden boot or RAM/FPS benchmark')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', help='native freestanding x86-64 game guest ELF')
    args = parser.parse_args()
    try:
        result = audit(args.elf)
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        result = dict(status='FAIL', errors=[str(exc)])
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result['status'] == 'PASS' else 1

if __name__ == '__main__':
    sys.exit(main())
