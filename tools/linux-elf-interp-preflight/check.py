#!/usr/bin/env python3
"""Fail-closed ELF64 x86-64 PT_INTERP metadata preflight; NOT a loader."""
import argparse
import json
import struct
from pathlib import Path

MAX_FILE = 64 * 1024 * 1024
MAX_INTERP = 4096

class InvalidELF(ValueError):
    pass

def inspect(data: bytes) -> dict:
    if len(data) < 64 or len(data) > MAX_FILE:
        raise InvalidELF('file size')
    if data[:4] != b'\x7fELF' or data[4:7] != b'\x02\x01\x01':
        raise InvalidELF('ELF64 little-endian version 1 required')
    if data[7] not in (0, 3) or any(data[8:16]):
        raise InvalidELF('unsupported identification')
    etype, machine, version = struct.unpack_from('<HHI', data, 16)
    if etype not in (2, 3) or machine != 62 or version != 1:
        raise InvalidELF('unsupported type/machine/version')
    entry, phoff = struct.unpack_from('<QQ', data, 24)
    ehsize, phentsize, phnum = struct.unpack_from('<HHH', data, 52)
    if ehsize != 64 or phentsize != 56 or not 1 <= phnum <= 128:
        raise InvalidELF('invalid program header layout')
    if phoff < 64 or phoff > len(data) or phnum > (len(data) - phoff) // phentsize:
        raise InvalidELF('program header out of bounds')
    interp = None
    loads = []
    for i in range(phnum):
        typ, flags, off, vaddr, _paddr, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * 56)
        if filesz > len(data) or off > len(data) - filesz:
            raise InvalidELF('segment outside file')
        if typ == 1:
            if filesz > memsz or (flags & 2 and flags & 1):
                raise InvalidELF('invalid or writable-executable load segment')
            if align not in (0, 1) and (align & (align - 1) or off % align != vaddr % align):
                raise InvalidELF('invalid load alignment')
            if memsz > (1 << 47) or vaddr > (1 << 47) - memsz:
                raise InvalidELF('virtual address overflow')
            loads.append((vaddr, vaddr + memsz))
        elif typ == 3:
            if interp is not None or not 2 <= filesz <= MAX_INTERP:
                raise InvalidELF('duplicate or oversized interpreter')
            raw = data[off:off + filesz]
            if raw[-1:] != b'\0' or b'\0' in raw[:-1]:
                raise InvalidELF('interpreter must have one terminal NUL')
            try:
                interp = raw[:-1].decode('utf-8', 'strict')
            except UnicodeDecodeError as e:
                raise InvalidELF('interpreter not UTF-8') from e
            parts = interp.split('/')
            if not interp.startswith('/') or any(p in ('', '.', '..') for p in parts[1:]) or '\\' in interp:
                raise InvalidELF('noncanonical interpreter path')
    if not loads:
        raise InvalidELF('no load segments')
    loads.sort()
    if any(a[1] > b[0] for a, b in zip(loads, loads[1:])):
        raise InvalidELF('overlapping load segments')
    return {'type': 'ET_EXEC' if etype == 2 else 'ET_DYN', 'entry': entry, 'interpreter': interp, 'load_segments': len(loads), 'status': 'metadata-only; not executable authorization'}

def main():
    p = argparse.ArgumentParser()
    p.add_argument('elf', type=Path)
    args = p.parse_args()
    try:
        with args.elf.open('rb') as f:
            data = f.read(MAX_FILE + 1)
        print(json.dumps(inspect(data), sort_keys=True))
    except (InvalidELF, OSError) as e:
        p.exit(1, f'rejected: {e}\n')

if __name__ == '__main__':
    main()
