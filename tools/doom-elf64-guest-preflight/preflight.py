#!/usr/bin/env python3
"""Conservative host-side x86-64 ELF64 preflight; NOT a guest loader."""
import argparse
import json
import pathlib
import struct
import sys

EHDR = struct.Struct("<16sHHIQQQIHHHHHH")
PHDR = struct.Struct("<IIQQQQQQ")
PT_LOAD, PT_DYNAMIC, PT_INTERP, PT_GNU_STACK = 1, 2, 3, 0x6474e551
PF_X, PF_W = 1, 2
MAX_INPUT = 64 * 1024 * 1024
MAX_PH = 128
MAX_U64 = (1 << 64) - 1
MAX_GUEST_BYTES = 512 * 1048576

class InvalidELF(ValueError):
    pass

def checked_end(start, size, label):
    if start < 0 or size < 0 or start > MAX_U64 - size:
        raise InvalidELF(label + ": 64-bit range overflow")
    return start + size

def inspect(data, guest_ram_bytes, other_reserved_bytes=0):
    if type(guest_ram_bytes) is not int or type(other_reserved_bytes) is not int:
        raise InvalidELF("memory budgets must be integer bytes (not booleans)")
    if guest_ram_bytes <= 0 or guest_ram_bytes > MAX_GUEST_BYTES:
        raise InvalidELF("guest RAM exceeds strict 512 MiB physical memory target or is invalid")
    if other_reserved_bytes < 0 or other_reserved_bytes >= guest_ram_bytes:
        raise InvalidELF("invalid guest/reserved budget")
    if len(data) > MAX_INPUT:
        raise InvalidELF("input exceeds 64 MiB")
    if len(data) < EHDR.size:
        raise InvalidELF("truncated ELF header")
    h = EHDR.unpack_from(data)
    ident, e_type, machine, version, entry, phoff, shoff, flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx = h
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1 or ident[6] != 1:
        raise InvalidELF("expected little-endian ELF64 v1")
    if e_type != 2 or machine != 62 or version != 1:
        raise InvalidELF("expected x86-64 ET_EXEC, not ET_REL/ET_DYN")
    if ehsize != EHDR.size or phentsize != PHDR.size or phnum == 0 or phnum > MAX_PH:
        raise InvalidELF("invalid ELF/program-header shape")
    if checked_end(phoff, phnum * phentsize, "program headers") > len(data):
        raise InvalidELF("program headers outside file")
    ranges = []
    page_permissions = []
    executable = []
    file_bytes = 0
    for i in range(phnum):
        typ, pflags, offset, vaddr, paddr, filesz, memsz, align = PHDR.unpack_from(data, phoff + i * phentsize)
        if typ in (PT_INTERP, PT_DYNAMIC):
            raise InvalidELF("interpreter/dynamic loading not supported by this profile")
        if typ == PT_GNU_STACK and (pflags & PF_X):
            raise InvalidELF("executable stack requested")
        if typ != PT_LOAD:
            continue
        if pflags & ~(1 | 2 | 4) or (pflags & (PF_W | PF_X)) == (PF_W | PF_X):
            raise InvalidELF("invalid/W+X load permissions")
        if not memsz or filesz > memsz:
            raise InvalidELF("empty load segment or filesz > memsz")
        if align not in (0, 1) and (align & (align - 1)) != 0:
            raise InvalidELF("segment alignment is not a power of two")
        if align > 1 and (vaddr - offset) % align:
            raise InvalidELF("file/virtual alignment mismatch")
        end = checked_end(vaddr, memsz, "segment virtual address")
        if checked_end(offset, filesz, "segment file range") > len(data):
            raise InvalidELF("segment file range outside file")
        # Avoid null pages and require canonical low-half addresses.
        if vaddr < 0x1000 or end > (1 << 47):
            raise InvalidELF("load address outside conservative user range")
        ranges.append((vaddr, end))
        page_permissions.append(((vaddr // 4096) * 4096, ((end + 4095) // 4096) * 4096, pflags))
        if pflags & PF_X:
            # The entry must identify file-backed instruction bytes rather
            # than an executable segment's zero-filled BSS tail.
            executable.append((vaddr, vaddr + filesz))
        file_bytes += filesz
    if not ranges:
        raise InvalidELF("no load segments")
    ranges.sort()
    for left, right in zip(ranges, ranges[1:]):
        if left[1] > right[0]:
            raise InvalidELF("overlapping load virtual ranges")
    for i, (lo, hi, flags) in enumerate(page_permissions):
        for other_lo, other_hi, other_flags in page_permissions[i + 1:]:
            if lo < other_hi and other_lo < hi:
                # One x86 page has one effective permission set. Even RX/R
                # or R/RW aliases would silently weaken one segment's
                # intended isolation when the loader maps both ranges.
                if flags != other_flags:
                    raise InvalidELF("load segments share a page with conflicting permissions")
    if not any(lo <= entry < hi for lo, hi in executable):
        raise InvalidELF("entry point not in file-backed executable load bytes")
    # This is an upper bound on direct loaded-image bytes, NOT real peak RSS:
    # page-rounded load regions can introduce extra consumption.
    page_ranges = sorted(((lo // 4096) * 4096, ((hi + 4095) // 4096) * 4096) for lo, hi in ranges)
    merged = []
    for lo, hi in page_ranges:
        if merged and lo <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
        else:
            merged.append((lo, hi))
    rounded_bytes = sum(hi - lo for lo, hi in merged)
    if rounded_bytes > guest_ram_bytes - other_reserved_bytes:
        raise InvalidELF("static image exceeds remaining declared guest memory")
    return {
        "classification": "host_preflight_only_not_guest_execution",
        "elf_type": "ET_EXEC", "architecture": "x86_64",
        "entry": entry, "load_segments": len(ranges),
        "load_mem_bytes": sum(hi - lo for lo, hi in ranges),
        "load_page_rounded_bytes": rounded_bytes,
        "load_file_bytes": file_bytes,
        "guest_ram_bytes_configured": guest_ram_bytes,
        "other_guest_bytes_reserved_assumed": other_reserved_bytes,
        "guest_boot_verified": False, "runtime_memory_measured": False,
        "fps_measured": False,
    }

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("elf", type=pathlib.Path)
    p.add_argument("--guest-mib", type=int, default=512)
    p.add_argument("--reserved-mib", type=int, required=True,
                   help="explicit assumed kernel/graphics/other guest reserve; NOT measured")
    args = p.parse_args(argv)
    try:
        if args.elf.stat().st_size > MAX_INPUT:
            raise InvalidELF("input exceeds 64 MiB")
        report = inspect(args.elf.read_bytes(), args.guest_mib * 1048576,
                         args.reserved_mib * 1048576)
        print(json.dumps({"valid": True, **report}, sort_keys=True))
        return 0
    except (InvalidELF, OSError) as exc:
        print(json.dumps({"valid": False, "error": str(exc),
                          "guest_boot_verified": False}), file=sys.stdout)
        return 1

if __name__ == "__main__":
    sys.exit(main())
