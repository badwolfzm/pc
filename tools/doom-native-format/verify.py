#!/usr/bin/env python3
"""Genuine pinned Doom ET_REL symbol closure: exactly snprintf/vsnprintf."""
import json
import pathlib
import subprocess
import sys
TARGET = {"snprintf", "vsnprintf"}
PROTECTED = {"system", "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "remove", "rename"}
def undefined(path):
    result = subprocess.check_output(["nm", "-P", "-g", "-u", str(path)], text=True)
    values = set()
    for line in result.splitlines():
        parts = line.split()
        if len(parts) != 2 or parts[1] != "U" or parts[0] in values:
            raise ValueError("unexpected nm row: " + line)
        values.add(parts[0])
    return values
def verify(before, after, output):
    old, new = undefined(before), undefined(after)
    if not TARGET.issubset(old):
        raise ValueError("original combined engine did not import both target symbols")
    if new != old - TARGET:
        raise ValueError("unexpected new/missing imports after formatting provider")
    if not PROTECTED.issubset(new):
        raise ValueError("protected file/process imports were improperly resolved")
    report = {"native_warden_boot_verified": False, "native_executable_built": False,
              "resolved_symbols": sorted(TARGET), "unresolved_before": len(old),
              "unresolved_after": len(new), "unresolved_symbols": sorted(new)}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, sort_keys=True))
if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify.py before.o after.o report.json")
    verify(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]))
