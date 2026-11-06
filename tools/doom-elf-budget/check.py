#!/usr/bin/env python3
"""Conservative ELF64 x86-64 PT_LOAD mapping budget preflight; no execution."""
import argparse
import json
import pathlib
import struct
import sys

PAGE = 4096
ELF = struct.Struct('<16sHHIQQQIHHHHHH')
PH = struct.Struct('<IIQQQQQQ')
PT_LOAD, PT_DYNAMIC, PF_X, PF_W = 1, 2, 1, 2
DEFAULT_LO, DEFAULT_HI = 0x800000, 0xA00000

class Invalid(Exception):
    pass

def aligned_down(x): return x & ~(PAGE - 1)
def aligned_up(x): return (x + PAGE - 1) & ~(PAGE - 1)

def inspect(blob, lo=DEFAULT_LO, hi=DEFAULT_HI, stack=131072, staging=307200):
    if not (0 <= lo < hi <= (1 << 64)) or lo % PAGE or hi % PAGE:
        raise Invalid('invalid page-aligned guest window')
    if stack < 0 or staging < 0:
        raise Invalid('negative reserve')
    if len(blob) < ELF.size or blob[:4] != b'\x7fELF' or blob[4:7] != b'\x02\x01\x01':
        raise Invalid('not ELF64 little-endian version 1')
    ident, typ, machine, version, entry, phoff, _, _, ehsize, phentsize, count, _, _, _ = ELF.unpack_from(blob)
    if typ != 2 or machine != 62 or version != 1 or ehsize != ELF.size or phentsize != PH.size:
        raise Invalid('requires fixed-address x86-64 ELF64 ET_EXEC with standard headers')
    if count == 0 or count > 128 or phoff > len(blob) or count * PH.size > len(blob) - phoff:
        raise Invalid('invalid bounded program header table')
    pages = {}
    segments = []
    has_entry = False
    for i in range(count):
        kind, flags, off, va, _, filesz, memsz, align = PH.unpack_from(blob, phoff + i * PH.size)
        if kind == PT_DYNAMIC:
            raise Invalid('dynamic ELF needs separate reviewed loader')
        if kind != PT_LOAD:
            continue
        if not memsz or filesz > memsz or off > len(blob) or filesz > len(blob) - off:
            raise Invalid(f'segment {i} empty, truncated or filesz exceeds memsz')
        if flags & PF_X and flags & PF_W:
            raise Invalid(f'segment {i} writable and executable')
        if va < lo or memsz > hi - va or (off % PAGE) != (va % PAGE):
            raise Invalid(f'segment {i} outside window or incongruent file offset')
        if align not in (0, 1) and (align & (align - 1) or (va - off) % align):
            raise Invalid(f'segment {i} invalid alignment')
        if flags & PF_X and va <= entry < va + memsz:
            has_entry = True
        start, end = aligned_down(va), aligned_up(va + memsz)
        for pg in range(start, end, PAGE):
            if pg in pages:
                raise Invalid(f'overlapping PT_LOAD pages {pg:#x}')
            pages[pg] = flags
        segments.append({'index': i, 'virtual_address': va, 'file_bytes': filesz, 'memory_bytes': memsz, 'pages': (end - start) // PAGE})
    if not pages or not has_entry:
        raise Invalid('no mapped executable entry')
    mapped = len(pages) * PAGE
    reserved = aligned_up(stack) + aligned_up(staging)
    capacity = hi - lo
    total = mapped + reserved
    if total > capacity:
        raise Invalid(f'mapped {mapped} + stack/staging {reserved} exceeds window {capacity}')
    return {'valid': True, 'entry': entry, 'window_bytes': capacity,
            'mapped_pages': len(pages), 'mapped_bytes': mapped,
            'stack_reserved_bytes': aligned_up(stack),
            'staging_reserved_bytes': aligned_up(staging),
            'unallocated_window_bytes': capacity - total,
            'segments': segments,
            'caveat': 'static ELF mapping estimate only; not actual runtime RAM, heap, page tables, WAD, kernel or physical peak'}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=pathlib.Path)
    p.add_argument('--window-low', type=lambda s: int(s, 0), default=DEFAULT_LO)
    p.add_argument('--window-high', type=lambda s: int(s, 0), default=DEFAULT_HI)
    p.add_argument('--stack-bytes', type=int, default=131072)
    p.add_argument('--staging-bytes', type=int, default=307200)
    a = p.parse_args()
    try:
        result = inspect(a.elf.read_bytes(), a.window_low, a.window_high, a.stack_bytes, a.staging_bytes)
    except (Invalid, OSError) as e:
        print(json.dumps({'valid': False, 'reason': str(e)}))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0

if __name__ == '__main__':
    sys.exit(main())
