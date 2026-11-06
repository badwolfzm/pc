#!/usr/bin/env python3
"""Conservative fixed-address ELF64 x86-64 guest image budget check. Not a loader."""
import argparse
import json
import struct
import sys

HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM = struct.Struct("<IIQQQQQQ")
PAGE = 4096
MAX_FILE = 128 * 1024 * 1024

class BudgetError(ValueError):
    pass

def round_up(n):
    return ((n + PAGE - 1) // PAGE) * PAGE

def evaluate(data, start, size=2097152, stack=131072, heap=262144, assets=0):
    if len(data) > MAX_FILE:
        raise BudgetError("ELF exceeds 128 MiB bound")
    if len(data) < HEADER.size:
        raise BudgetError("short ELF header")
    ident, typ, machine, version, entry, poff, _, _, ehsize, psize, n, _, _, _ = HEADER.unpack_from(data)
    if ident[:7] != b"\x7fELF\x02\x01\x01" or typ != 2 or machine != 62 or version != 1:
        raise BudgetError("requires little-endian fixed-address x86-64 ELF64 ET_EXEC")
    if ehsize != HEADER.size or psize != PROGRAM.size or not 1 <= n <= 128:
        raise BudgetError("invalid header sizing")
    if poff > len(data) or n * psize > len(data) - poff:
        raise BudgetError("truncated program headers")
    if any(not isinstance(v, int) or v < 0 or v > (1 << 40) for v in (start, size, stack, heap, assets)):
        raise BudgetError("invalid budget")
    if size < PAGE or start % PAGE or start + size > (1 << 47):
        raise BudgetError("invalid address window")
    ranges = []
    segments = []
    executable_entry = False
    for i in range(n):
        kind, flags, offset, vaddr, _, filesz, memsz, alignment = PROGRAM.unpack_from(data, poff + i * psize)
        if kind != 1:
            continue
        if not memsz or filesz > memsz or offset > len(data) or filesz > len(data) - offset:
            raise BudgetError("bad PT_LOAD file or memory size")
        if alignment > 1 and ((alignment & (alignment - 1)) or (vaddr - offset) % alignment):
            raise BudgetError("invalid PT_LOAD alignment")
        if vaddr < start or vaddr + memsz > start + size:
            raise BudgetError("PT_LOAD outside window")
        low, high = (vaddr // PAGE) * PAGE, round_up(vaddr + memsz)
        ranges.append((low, high))
        segments.append({"index": i, "vaddr": vaddr, "file_bytes": filesz, "mem_bytes": memsz, "page_bytes": high-low})
        if flags & 1 and vaddr <= entry < vaddr + memsz:
            executable_entry = True
    if not segments or not executable_entry:
        raise BudgetError("no executable segment containing entry")
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise BudgetError("overlapping PT_LOAD pages")
    mapped = sum(b-a for a,b in ranges)
    reserved = sum(map(round_up, (stack, heap, assets)))
    free = size - mapped - reserved
    if free < 0:
        raise BudgetError("address budget exceeded by %d bytes" % -free)
    return {"entry": entry, "window_bytes": size, "mapped_bytes": mapped,
            "reserved_stack_bytes": round_up(stack), "reserved_heap_bytes": round_up(heap),
            "reserved_asset_bytes": round_up(assets), "remaining_bytes": free,
            "segments": segments,
            "caveat": "Aggregate virtual-address preflight only. Not a placement, physical RAM, Warden boot or FPS proof."}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("elf")
    p.add_argument("--window-start", required=True, type=lambda s: int(s, 0))
    p.add_argument("--window-bytes", default=2097152, type=lambda s: int(s, 0))
    p.add_argument("--stack-bytes", default=131072, type=lambda s: int(s, 0))
    p.add_argument("--heap-bytes", default=262144, type=lambda s: int(s, 0))
    p.add_argument("--asset-bytes", default=0, type=lambda s: int(s, 0))
    a = p.parse_args()
    try:
        with open(a.elf, "rb") as f:
            data = f.read(MAX_FILE + 1)
        report = evaluate(data, a.window_start, a.window_bytes, a.stack_bytes, a.heap_bytes, a.asset_bytes)
        print(json.dumps({"ok": True, **report}, sort_keys=True))
        return 0
    except (OSError, BudgetError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1

if __name__ == "__main__":
    sys.exit(main())
