#!/usr/bin/env python3
"""Prove one genuine Doom ET_REL import is resolved, with no new imports."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

TARGET = "__ctype_toupper_loc"


def unresolved(obj):
    result = subprocess.run(["nm", "-g", "-u", str(obj)], capture_output=True,
                            text=True, check=True)
    found = set()
    for line in result.stdout.splitlines():
        match = re.fullmatch(r"\s*U\s+(\S+)\s*", line)
        if not match:
            raise ValueError("unexpected nm line: " + line)
        found.add(match.group(1))
    return found


def verify(before, after):
    old = unresolved(before)
    new = unresolved(after)
    if TARGET not in old or TARGET in new:
        raise ValueError("target must be unresolved before, and resolved after")
    if old - new != {TARGET} or new - old:
        raise ValueError("symbol changes outside exactly one target import")
    return {"resolved_import": TARGET, "unresolved_before": len(old),
            "unresolved_after": len(new), "unresolved_after_symbols": sorted(new),
            "before_sha256": hashlib.sha256(before.read_bytes()).hexdigest(),
            "after_sha256": hashlib.sha256(after.read_bytes()).hexdigest(),
            "relocatable_link_only": True, "warden_native_game_booted": False,
            "guest_ram_mib_measured": None, "fps_measured": None}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("before", type=pathlib.Path)
    p.add_argument("after", type=pathlib.Path)
    p.add_argument("output", type=pathlib.Path)
    args = p.parse_args()
    report = verify(args.before, args.after)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
