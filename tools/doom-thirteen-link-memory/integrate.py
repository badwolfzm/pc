#!/usr/bin/env python3
"""Pinned real Doom ET_REL: 13 merged providers, no host privilege masking.

Does NOT produce a guest executable or claim physical memory residency.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
TEN = frozenset(("abs", "fabs", "strcasecmp", "strncasecmp", "strncpy",
                 "strstr", "malloc", "calloc", "realloc", "free"))
THREE = frozenset(("__ctype_toupper_loc", "__errno_location", "strtol"))
TARGET = TEN | THREE
PROTECTED = frozenset(("system", "fopen", "fclose", "fread", "fwrite",
                       "fseek", "ftell", "remove", "rename"))
SOURCES = {
    "pure": "tools/doom-pure-crt-symbols/pure.c",
    "strings": "tools/doom-mini-crt/mini_extra.c",
    "arena": "tools/doom-freestanding-arena/arena.c",
    "heap": "tools/doom-native-heap-closure/heap_alias.c",
    "ctype": "tools/doom-native-ctype/doom_ctype.c",
    "errno": "tools/doom-native-errno/errno.c",
    "strtol": "tools/doom-native-strtol/strtol.c",
}
OWNERS = {
    "pure": frozenset(("abs", "fabs")),
    "strings": frozenset(("strcasecmp", "strncasecmp", "strncpy", "strstr")),
    "heap": frozenset(("malloc", "calloc", "realloc", "free")),
    "ctype": frozenset(("__ctype_toupper_loc",)),
    "errno": frozenset(("__errno_location",)),
    "strtol": frozenset(("strtol",)),
}


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def strong_owners(objects):
    owners = {}
    for label, path in objects:
        out = subprocess.run(["nm", "-P", "-g", "--defined-only", str(path)],
                             capture_output=True, text=True, check=True, timeout=20).stdout
        for line in out.splitlines():
            columns = line.split()
            if len(columns) < 2:
                raise ValueError("invalid nm definition line")
            symbol, typ = columns[:2]
            if typ in ("W", "V", "w", "v"):
                continue
            if typ not in ("T", "D", "B", "R", "C", "A"):
                raise ValueError("unexpected definition type: " + line)
            if symbol in owners:
                raise ValueError("duplicate strong definition: " + symbol)
            owners[symbol] = label
    return owners


def validate(before, after, owners):
    old, new = set(before), set(after)
    if len(TARGET) != 13 or not TARGET.issubset(old):
        raise ValueError("baseline does not require 13 target imports")
    if not PROTECTED.issubset(old) or not PROTECTED.issubset(new):
        raise ValueError("protected host operations must stay unresolved")
    if old - new != TARGET or new - old:
        raise ValueError("unexpected symbol delta")
    for label, defs in OWNERS.items():
        for sym in defs:
            if owners.get(sym) != label:
                raise ValueError("provider ownership mismatch: " + sym)
    return {"original_unresolved": len(old), "remaining_unresolved": len(new),
            "closed_exactly": sorted(TARGET), "remaining_symbols": sorted(new)}


def section_footprint(path):
    """Parse actual ET_REL ELF64 little endian section headers; SHF_ALLOC only."""
    data = path.read_bytes()
    if len(data) < 64 or len(data) > 128 * 1024 * 1024:
        raise ValueError("ELF missing or oversized")
    if data[:4] != b"\\x7fELF" or data[4] != 2 or data[5] != 1 or data[6] != 1:
        raise ValueError("requires ELF64 little endian")
    typ, machine = struct.unpack_from("<HH", data, 16)
    if typ != 1 or machine != 62:
        raise ValueError("requires x86_64 ET_REL")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 58)
    if shentsize != 64 or shnum == 0 or shnum > 4096 or shoff > len(data) or shnum > (len(data) - shoff) // 64:
        raise ValueError("invalid section table")
    total = file_bytes = bss = 0
    per_section = []
    for i in range(shnum):
        _, shtype, flags, _, off, size, _, _, align, _ = struct.unpack_from("<IIQQQQIIQQ", data, shoff + 64*i)
        if not flags & 2:
            continue
        if size > 512 * 1024 * 1024 or (align and (align & (align - 1))):
            raise ValueError("invalid allocatable section size/alignment")
        if shtype != 8 and (off > len(data) or size > len(data) - off):
            raise ValueError("truncated file-backed section")
        total += size
        if shtype == 8: bss += size
        else: file_bytes += size
        per_section.append({"index": i, "type": "NOBITS" if shtype == 8 else "FILE",
                            "size": size, "align": align})
    if total != bss + file_bytes or not per_section:
        raise ValueError("empty or inconsistent allocatable sections")
    return {"allocatable_total_section_bytes": total,
            "allocatable_file_backed_bytes": file_bytes,
            "allocatable_nobits_bytes": bss,
            "section_count": len(per_section),
            "note": "Section sizes only, no PT_LOAD alignment/paging/stack WAD or physical residency"}


def run(source, output):
    if not source.is_dir():
        raise ValueError("missing upstream source")
    gate = module(ROOT / "tools/doom-native-link-gate/gate.py", "genuine_doom_gate")
    if gate.PIN != PIN:
        raise ValueError("upstream pin changed")
    output.mkdir(parents=True, exist_ok=True)
    baseline = gate.run(source, output / "baseline")
    if baseline["real_upstream_source_count"] != 79 or baseline["warden_component_count"] != 5:
        raise ValueError("unexpected real engine inventory")
    files = [("engine-and-warden", output / "baseline/all-objects.o")]
    sources = {}
    for label, rel in SOURCES.items():
        src = ROOT / rel
        if not src.is_file() or src.is_symlink():
            raise ValueError("provider source missing or symlinked")
        obj = output / (label + ".o")
        gate.execute(["cc", *gate.CFLAGS, "-DDOOM_ARENA_BYTES=1048576",
                      "-I" + str(ROOT / "tools/doom-freestanding-arena"),
                      "-c", str(src), "-o", str(obj)])
        files.append((label, obj))
        sources[label] = hashlib.sha256(src.read_bytes()).hexdigest()
    owners = strong_owners(files)
    dest = output / "doom-13-providers.o"
    gate.execute(["ld", "-r", "-o", str(dest), *[str(f) for _, f in files]])
    remaining = gate.missing_symbols(gate.execute(["nm", "-g", "-u", str(dest)]))
    verdict = validate(baseline["unresolved_symbols"], remaining, owners)
    metrics = section_footprint(dest)
    report = {
        "source": "real pinned Doomgeneric + merged Warden adapters + merged freestanding providers",
        "upstream_commit": PIN,
        "selected_sources_sha256": sources,
        "combined_object_sha256": hashlib.sha256(dest.read_bytes()).hexdigest(),
        "static_arena_reserved_bytes": 1048576,
        "memory_sections": metrics,
        "provider_owner": {x: owners[x] for x in sorted(TARGET)},
        **verdict,
        "build_type": "x86-64 ET_REL; not executable",
        "warden_qemu_game_boot": False,
        "guest_512m_physical_residency": None,
        "fps": None,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"original_unresolved": verdict["original_unresolved"],
                      "remaining_unresolved": verdict["remaining_unresolved"],
                      "static_section_bytes": metrics["allocatable_total_section_bytes"]}, sort_keys=True))
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    try:
        run(a.source, a.output)
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as e:
        p.error(str(e))


if __name__ == "__main__":
    main()
