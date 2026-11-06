#!/usr/bin/env python3
"""Fail-closed static ELF64 x86-64 PT_LOAD budget gate for genuine Doom candidate.

Not a guest loader, runtime security boundary, or proof of guest execution.
"""
import argparse
import json
import struct
from pathlib import Path

MIB = 1024 * 1024
MAX_GUEST = 512 * MIB
HEADER = struct.Struct('<16sHHIQQQIHHHHHH')
PHDR = struct.Struct('<IIQQQQQQ')

def inspect(data, guest_bytes=MAX_GUEST, stack_bytes=MIB):
    if not (0 < guest_bytes <= MAX_GUEST and 0 < stack_bytes <= guest_bytes):
        raise ValueError('invalid guest/stack budget')
    if len(data) < HEADER.size:
        raise ValueError('truncated ELF header')
    ident, typ, machine, version, entry, phoff, _, _, ehsize, phentsize, phnum, *_ = HEADER.unpack_from(data)
    if ident[:4] != b'\x7fELF' or ident[4:7] != b'\x02\x01\x01':
        raise ValueError('expected little-endian ELF64')
    if typ != 2 or machine != 62 or version != 1:
        raise ValueError('expected x86-64 ET_EXEC')
    if ehsize != HEADER.size or phentsize != PHDR.size or not 0 < phnum <= 128:
        raise ValueError('invalid program header geometry')
    if phoff > len(data) or phnum * phentsize > len(data) - phoff:
        raise ValueError('truncated program header table')
    segments = []
    for i in range(phnum):
        typ, flags, off, va, _, filesz, memsz, align = PHDR.unpack_from(data, phoff + i * phentsize)
        if typ in (2, 3, 7):  # dynamic, interpreter, TLS: not supported by this native gate
            raise ValueError('unsupported dynamic/interpreter/TLS program header')
        if typ != 1:
            continue
        if not memsz or filesz > memsz or off > len(data) or filesz > len(data) - off:
            raise ValueError('invalid PT_LOAD file/memory extent')
        if align not in (0, 1) and (align & (align - 1) or (va - off) % align):
            raise ValueError('invalid PT_LOAD alignment')
        if va > (1 << 47) - 1 or memsz > (1 << 47) - va:
            raise ValueError('noncanonical/overflowing virtual mapping')
        if flags & 2 and flags & 1:
            raise ValueError('writable executable segment unsupported')
        segments.append((va, va + memsz, flags, memsz))
    if not segments:
        raise ValueError('no PT_LOAD segments')
    segments.sort()
    for a, b in zip(segments, segments[1:]):
        if a[1] > b[0]:
            raise ValueError('overlapping PT_LOAD virtual extents')
        # Even disjoint byte extents can share a page, causing loader
        # permission conflicts or accidental cross-segment overwrites.
        if (a[1] + 4095) // 4096 > b[0] // 4096:
            raise ValueError('PT_LOAD segments share a virtual page')
    if not any(start <= entry < end and flags & 1 for start, end, flags, _ in segments):
        raise ValueError('entry point not within executable PT_LOAD')
    # Conservatively count page-rounded independent segments, not only file bytes.
    pages = sum(((end + 4095) // 4096 - start // 4096) * 4096 for start, end, _, _ in segments)
    if pages + stack_bytes > guest_bytes:
        raise ValueError('PT_LOAD plus stack exceeds guest budget')
    return {'entry': hex(entry), 'segments': len(segments),
            'segment_pages_bytes': pages, 'stack_budget_bytes': stack_bytes,
            'guest_budget_bytes': guest_bytes}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('--guest-mib', type=int, default=512)
    p.add_argument('--stack-mib', type=int, default=1)
    args = p.parse_args()
    try:
        result = inspect(args.elf.read_bytes(), args.guest_mib * MIB, args.stack_mib * MIB)
    except (ValueError, OSError) as exc:
        p.error(str(exc))
    print(json.dumps(result, sort_keys=True))

if __name__ == '__main__':
    main()
