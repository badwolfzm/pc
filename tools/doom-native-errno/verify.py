#!/usr/bin/env python3
"""Require genuine freestanding Doom+strtol ET_REL to lose precisely two imports."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

TARGETS = frozenset(("__errno_location", "strtol"))
DANGEROUS = frozenset(("system", "fopen", "fclose", "fread", "fwrite", "remove", "rename"))


def unresolved(path):
    r = subprocess.run(["nm", "-P", "-g", "-u", str(path)],
                       check=True, capture_output=True, text=True)
    out = set()
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[1] != "U" or parts[0] in out:
            raise ValueError("unexpected nm output: " + line)
        out.add(parts[0])
    return out


def defined(path):
    r = subprocess.run(["nm", "-P", "-g", "--defined-only", str(path)],
                       check=True, capture_output=True, text=True)
    names = set()
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[1] not in ("T", "B", "D", "R", "C", "W", "V") or parts[0] in names:
            raise ValueError("unexpected defined symbol: " + line)
        names.add(parts[0])
    return names


def check(before, after, errno, strtol):
    if defined(errno) != {"__errno_location"} or unresolved(errno):
        raise ValueError("errno provider has unexpected exports/imports")
    if defined(strtol) != {"strtol"} or unresolved(strtol) != {"__errno_location"}:
        raise ValueError("strtol dependency contract changed")
    old, new = unresolved(before), unresolved(after)
    if not TARGETS.issubset(old) or new != old - TARGETS:
        raise ValueError("not exactly the expected two-symbol real Doom closure")
    if not DANGEROUS.issubset(new):
        raise ValueError("protected host calls must remain unresolved")
    return {
        "closed": sorted(TARGETS),
        "original_unresolved": len(old),
        "remaining_unresolved": len(new),
        "remaining_symbols": sorted(new),
        "source_sha256": hashlib.sha256(Path(before).read_bytes()).hexdigest(),
        "combined_sha256": hashlib.sha256(Path(after).read_bytes()).hexdigest(),
        "scope": "single-thread per-image errno; ET_REL only",
        "native_warden_doom_boot": False,
        "measured_guest_ram_mib": None,
        "measured_fps": None
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for a in ("before", "after", "errno", "strtol", "report"):
        p.add_argument(a, type=Path)
    args = p.parse_args()
    result = check(args.before, args.after, args.errno, args.strtol)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
