#!/usr/bin/env python3
"""Static, fail-closed ELF64 x86-64 load-span budget check. NOT an ELF loader."""
import argparse
import json
import struct
import sys

MAX_FILE = 128 * 1024 * 1024
MAX_PH = 512

class ELFError(ValueError):
    pass

def inspect(data, limit):
    if len(data) < 64 or len(data) > MAX_FILE:
        raise ELFError("ELF size outside permitted range")
    if data[:4] != b"\x7fELF" or data[4:7] != b"\x02\x01\x01":
        raise ELFError("requires little-endian ELF64 version 1")
    typ, machine, version = struct.unpack_from("<HHI", data, 16)
    if machine != 62 or version != 1 or typ not in (2, 3):
        raise ELFError("requires x86-64 ET_EXEC or ET_DYN")
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    ehsize, phentsize, phnum = struct.unpack_from("<HHH", data, 52)
    if ehsize != 64 or phentsize != 56 or not 1 <= phnum <= MAX_PH:
        raise ELFError("invalid program header layout")
    if phoff > len(data) or phnum * phentsize > len(data) - phoff:
        raise ELFError("program headers outside file")
    segments = []
    dynamic = interpreter = False
    for i in range(phnum):
        kind, flags, off, va, _, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, phoff + i*56)
        if kind == 2: dynamic = True
        if kind == 3: interpreter = True
        if kind != 1: continue
        if filesz > memsz or off > len(data) or filesz > len(data) - off:
            raise ELFError("invalid LOAD size or file bounds")
        if not memsz: continue
        if va + memsz >= 1 << 64:
            raise ELFError("LOAD address overflow")
        if align not in (0, 1) and (align & (align - 1) or (va-off) % align):
            raise ELFError("invalid LOAD alignment")
        segments.append({"start": va, "end": va+memsz, "file_bytes": filesz,
                         "memory_bytes": memsz, "flags": flags})
    if not segments:
        raise ELFError("no nonempty LOAD segments")
    lo = min(s["start"] for s in segments)
    hi = max(s["end"] for s in segments)
    span = hi - lo
    if span > limit:
        raise ELFError(f"LOAD span {span} exceeds budget {limit}")
    ordered = sorted(segments, key=lambda s: s["start"])
    for a, b in zip(ordered, ordered[1:]):
        if a["end"] > b["start"]:
            raise ELFError("overlapping LOAD segments")
    if not any(s["start"] <= entry < s["end"] and s["flags"] & 1 for s in ordered):
        raise ELFError("entry not in executable LOAD segment")
    return {"type": "ET_EXEC" if typ == 2 else "ET_DYN", "entry": entry,
            "load_count": len(segments), "load_span_bytes": span, "start": lo,
            "end": hi, "sum_memory_bytes": sum(s["memory_bytes"] for s in segments),
            "limit_bytes": limit, "has_dynamic_segment": dynamic,
            "has_interpreter": interpreter, "segments": ordered,
            "warning": "Excludes stack, heap, runtime libraries, framebuffer, WAD and page tables. ET_DYN relocations not analyzed."}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("elf")
    p.add_argument("--limit-mib", type=int, default=2)
    args = p.parse_args()
    if not 1 <= args.limit_mib <= 4096:
        p.error("limit-mib must be 1..4096")
    try:
        with open(args.elf, "rb") as f:
            data = f.read(MAX_FILE + 1)
        print(json.dumps(inspect(data, args.limit_mib*1024*1024), sort_keys=True, indent=2))
        return 0
    except (OSError, ELFError) as e:
        print(f"ELF budget check failed: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
