#!/usr/bin/env python3
"""Verify exactly one real pinned Doom ET_REL import closed, without hiding host calls."""
import argparse
import hashlib
import json
import pathlib
import subprocess

TARGET = "__ctype_toupper_loc"


def symbols(path):
    output = subprocess.check_output(["nm", "-P", "-g", str(path)], text=True)
    defined, undefined = set(), set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2:
            raise ValueError("unrecognized nm line: " + line)
        name, kind = fields[:2]
        if kind == "U":
            undefined.add(name)
        elif kind.upper() in {"T", "D", "B", "R", "W", "V"}:
            defined.add(name)
        else:
            raise ValueError("unrecognized nm symbol type: " + line)
    return defined, undefined


def verify(before, after, provider, report):
    provider_defined, provider_missing = symbols(provider)
    if provider_defined != {TARGET} or provider_missing:
        raise ValueError("provider must export exactly one symbol and import none")
    _, original_missing = symbols(before)
    _, remaining_missing = symbols(after)
    if TARGET not in original_missing:
        raise ValueError("real Doom object did not require the claimed import")
    if remaining_missing != original_missing - {TARGET}:
        raise ValueError("unexpected additional resolved or introduced symbols")
    dangerous = {"system", "fopen", "fread", "fwrite", "remove", "rename"}
    if not dangerous.issubset(remaining_missing):
        raise ValueError("unsafe host I/O/process imports must remain unresolved")
    result = {
        "provider": TARGET,
        "provider_sha256": hashlib.sha256(provider.read_bytes()).hexdigest(),
        "resolved": [TARGET],
        "introduced": [],
        "remaining_missing_count": len(remaining_missing),
        "remaining_missing": sorted(remaining_missing),
        "ctype_locale": "fixed C locale; ASCII uppercase only",
        "target": "x86-64 glibc-header native ABI",
        "real_upstream_engine_partially_linked": True,
        "native_guest_executable_built": False,
        "native_warden_boot_verified": False,
        "guest_ram_peak_measured": None,
        "fps_measured": None,
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, sort_keys=True))
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("before", type=pathlib.Path)
    p.add_argument("after", type=pathlib.Path)
    p.add_argument("provider", type=pathlib.Path)
    p.add_argument("report", type=pathlib.Path)
    a = p.parse_args()
    verify(a.before, a.after, a.provider, a.report)
