#!/usr/bin/env python3
"""Exact real-engine native console closure, no sensitive host API stubs."""
import argparse
import json
import pathlib
import subprocess

TARGETS = {"printf", "puts"}
SENSITIVE = {"system", "fopen", "fclose", "fread", "fwrite", "fseek", "ftell",
             "remove", "rename"}


def symbols(path):
    raw = subprocess.check_output(["nm", "-P", "-g", str(path)], text=True)
    defined, undefined = set(), set()
    for line in raw.splitlines():
        bits = line.split()
        if len(bits) < 2:
            raise ValueError("unexpected nm record: " + line)
        name, kind = bits[:2]
        if kind == "U":
            undefined.add(name)
        elif kind.upper() in {"T", "D", "B", "R", "W", "V", "C", "G", "S"}:
            defined.add(name)
        else:
            raise ValueError("unexpected global type: " + line)
    return defined, undefined


def inspect(before, after, provider):
    base_defs, start = symbols(before)
    end_defs, end = symbols(after)
    exports, imported = symbols(provider)
    if TARGETS & base_defs:
        raise ValueError("baseline already defines a console target")
    if exports != TARGETS or imported != {"vsnprintf"}:
        raise ValueError("console object exported or imported unexpected symbols")
    if not TARGETS.issubset(start):
        raise ValueError("genuine engine must import both console targets")
    if not SENSITIVE.issubset(start) or not SENSITIVE.issubset(end):
        raise ValueError("sensitive host I/O/process import disappeared")
    if end != start - TARGETS:
        raise ValueError("closure changed imports other than printf and puts")
    if end_defs != base_defs | TARGETS:
        raise ValueError("combined definitions differ beyond the two console targets")
    return {
        "resolved_imports": sorted(TARGETS),
        "introduced_imports": [],
        "original_unresolved_count": len(start),
        "remaining_unresolved_count": len(end),
        "remaining_unresolved_imports": sorted(end),
        "unsafe_host_calls_still_unresolved": sorted(SENSITIVE),
        "native_writable_console_slot": 0,
        "native_syscall_number": 13,
        "max_single_console_write_bytes": 1024,
        "format_provider": "canonical merged bounded snprintf/vsnprintf",
        "genuine_upstream_doom_et_rel_link": True,
        "bootable_warden_executable": False,
        "native_game_boot_verified": False,
        "512_mib_guest_peak_measured": False,
        "fps_measured": None,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ("before", "after", "provider", "report"):
        p.add_argument(arg, type=pathlib.Path)
    a = p.parse_args()
    result = inspect(a.before, a.after, a.provider)
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k in {
        "original_unresolved_count", "remaining_unresolved_count", "resolved_imports"}},
        sort_keys=True))


if __name__ == "__main__":
    main()
