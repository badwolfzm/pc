#!/usr/bin/env python3
"""Read-only Warden ELF preflight. Never executes or loads the inspected file."""
import argparse
import json
import struct
import sys
from pathlib import Path

PAGE = 4096
MAX_SEGMENTS = 8
WINDOWS = {"linux": (0x400000, 0x2000000), "native": (0x800000, 0x900000)}
PT_LOAD, PT_DYNAMIC, PT_INTERP = 1, 2, 3
PF_X, PF_W = 1, 2


def inspect_bytes(data, kind="linux"):
    """Match warden-elf's documented static loader guards; no security decision."""
    if kind not in WINDOWS:
        raise ValueError("unknown Warden executable window")
    lo, hi = WINDOWS[kind]
    out = {"target": kind, "classification": "blocked", "candidate_for_current_loader": False,
           "reasons": [], "facts": {}, "note": "Preflight only; no execution or kernel acceptance is implied."}

    def blocked(reason):
        if reason not in out["reasons"]:
            out["reasons"].append(reason)

    if len(data) < 64:
        blocked("truncated_elf_header")
        return out
    if data[:4] != b"\x7fELF":
        blocked("bad_magic")
        return out
    if data[4:6] != b"\x02\x01":
        blocked("requires_elf64_little_endian")
        return out

    etype, machine = struct.unpack_from("<HH", data, 16)
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    out["facts"].update(elf_type=etype, machine=machine, entry=entry, program_headers=phnum,
                        virtual_window=[lo, hi])
    if etype != 2:
        blocked("requires_static_et_exec_not_pie")
    if machine != 62:
        blocked("requires_x86_64")
    if phentsize != 56:
        blocked("invalid_program_header_entry_size")
        return out
    if phoff > len(data) or phnum > (len(data) - phoff) // 56:
        blocked("program_header_table_out_of_bounds")
        return out

    loads = []
    has_interp = False
    has_dynamic = False
    for i in range(phnum):
        off = phoff + i * 56
        ptype, flags = struct.unpack_from("<II", data, off)
        fileoff, vaddr, _paddr, filesz, memsz, _align = struct.unpack_from("<QQQQQQ", data, off + 8)
        if ptype == PT_INTERP:
            has_interp = True
            if filesz == 0 or fileoff > len(data) or filesz > len(data) - fileoff:
                blocked("invalid_interpreter_path_bounds")
            else:
                raw = data[fileoff:fileoff + filesz]
                if not raw.endswith(b"\0") or b"\0" in raw[:-1]:
                    blocked("invalid_interpreter_string")
                else:
                    out["facts"]["interpreter"] = raw[:-1].decode("utf-8", "replace")
        if ptype == PT_DYNAMIC:
            has_dynamic = True
        if ptype != PT_LOAD or memsz == 0:
            continue
        loads.append((vaddr, memsz, flags))
        if filesz > memsz:
            blocked("segment_filesz_exceeds_memsz")
        if flags & (PF_W | PF_X) == (PF_W | PF_X):
            blocked("writable_executable_segment")
        if vaddr < lo or memsz > hi - lo or vaddr > hi - memsz:
            blocked("segment_outside_warden_window")
        if fileoff > len(data) or filesz > len(data) - fileoff:
            blocked("segment_file_bounds_invalid")

    if has_interp:
        blocked("pt_interp_dynamic_loader_unavailable")
    if has_dynamic:
        blocked("pt_dynamic_not_supported_as_general_linux_linking")
    if len(loads) > MAX_SEGMENTS:
        blocked("too_many_load_segments")
    for i, (v, mem, flags) in enumerate(loads):
        left = v // PAGE
        right = (v + mem - 1) // PAGE if mem <= (1 << 64) - v else (1 << 64) // PAGE
        for prev_v, prev_mem, _ in loads[:i]:
            prev_left = prev_v // PAGE
            prev_right = (prev_v + prev_mem - 1) // PAGE if prev_mem <= (1 << 64) - prev_v else (1 << 64) // PAGE
            if left <= prev_right and prev_left <= right:
                blocked("page_granular_segment_overlap")
    if not any((flags & PF_X) and v <= entry < v + mem
               for v, mem, flags in loads if mem <= (1 << 64) - v):
        blocked("entry_not_in_executable_segment")

    out["facts"]["load_segments"] = len(loads)
    out["facts"]["has_pt_interp"] = has_interp
    out["facts"]["has_pt_dynamic"] = has_dynamic
    if not out["reasons"]:
        out["classification"] = "static_elf_loader_candidate"
        out["candidate_for_current_loader"] = True
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description="Preflight Linux ELF file vs current Warden loader (read-only)")
    p.add_argument("file", type=Path)
    p.add_argument("--target", choices=WINDOWS, default="linux")
    args = p.parse_args(argv)
    try:
        # Limit the inspection data size to defend automation from oversized inputs.
        if args.file.stat().st_size > 256 * 1024 * 1024:
            raise ValueError("file exceeds 256 MiB preflight limit")
        result = inspect_bytes(args.file.read_bytes(), args.target)
    except (OSError, ValueError) as exc:
        print(json.dumps({"classification": "error", "error": str(exc)}, sort_keys=True))
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["candidate_for_current_loader"] else 1


if __name__ == "__main__":
    sys.exit(main())
