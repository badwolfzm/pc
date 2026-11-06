#!/usr/bin/env python3
"""Require exactly snprintf+vsnprintf closure in a genuine Doom ET_REL link."""
import argparse
import json
import pathlib
import subprocess

TARGETS = {"snprintf", "vsnprintf"}
PROTECTED = {"system", "fopen", "fread", "fwrite", "remove", "rename"}


def symbols(path):
    data = subprocess.check_output(["nm", "-P", "-g", str(path)], text=True)
    defined, undefined = set(), set()
    for line in data.splitlines():
        parts = line.split()
        if len(parts) < 2:
            raise ValueError(f"unexpected nm output: {line!r}")
        name, kind = parts[:2]
        if kind == "U":
            undefined.add(name)
        elif kind.upper() in {"T", "D", "B", "R", "W", "V"}:
            defined.add(name)
        else:
            raise ValueError(f"unexpected nm symbol type: {line!r}")
    return defined, undefined


def verify(before, after, provider, report):
    defined, provider_undefined = symbols(provider)
    if defined != TARGETS or provider_undefined:
        raise ValueError("format provider exports extra symbols or imports runtime symbols")
    _, before_missing = symbols(before)
    _, after_missing = symbols(after)
    if not TARGETS.issubset(before_missing):
        raise ValueError("real Doom import set must contain both format symbols")
    if after_missing != before_missing - TARGETS:
        raise ValueError("expected only snprintf/vsnprintf to close")
    if not PROTECTED.issubset(after_missing):
        raise ValueError("protected process/file host functions were hidden")
    out = {
        "verified_closed_symbols": sorted(TARGETS),
        "introduced_imports": [],
        "missing_before": len(before_missing),
        "missing_after": len(after_missing),
        "remaining_imports": sorted(after_missing),
        "format_contract": "C-locale integers/ASCII/strings only; fail closed on unsupported",
        "build_type": "ELF ET_REL; not a native Warden executable",
        "warden_guest_boot_verified": False,
        "fps_measured": None,
        "guest_ram_measured": None,
        "albion_compatible": False,
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(json.dumps(out, sort_keys=True))
    return out


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ("before", "after", "provider", "report"):
        p.add_argument(arg, type=pathlib.Path)
    x = p.parse_args()
    verify(x.before, x.after, x.provider, x.report)
