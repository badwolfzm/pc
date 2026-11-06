#!/usr/bin/env python3
"""Verify real Doom relocatable link loses exactly the six pure C imports."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

EXPECTED = frozenset(("abs", "fabs", "strcasecmp", "strncasecmp", "strstr", "strncpy"))
SENSITIVE = frozenset(("system", "fopen", "fread", "fwrite", "malloc", "free"))
def symbols(output):
    found = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) != 2 or parts[0] != "U":
            raise ValueError("unexpected nm unresolved format: " + line)
        if parts[1] in found:
            raise ValueError("duplicate undefined symbol")
        found.add(parts[1])
    return found

def verify(original, updated):
    if not EXPECTED.issubset(original):
        raise ValueError("pinned Doom baseline no longer includes expected imports")
    if updated != original - EXPECTED:
        raise ValueError("unexpected symbols added, removed or retained")
    if not SENSITIVE.issubset(updated):
        raise ValueError("sensitive service unexpectedly resolved; review security boundary")
    return {"status": "PASS", "baseline_unresolved": len(original),
            "after_pure_crt_unresolved": len(updated),
            "removed_pure_symbols": sorted(EXPECTED),
            "unresolved_security_sensitive": sorted(SENSITIVE),
            "native_executable_built": False,
            "native_warden_boot_verified": False,
            "scope": "true relocatable partial-link with real pinned Doom engine, not a guest boot"}

def cmd(*args):
    return subprocess.check_output(args, text=True)

def run(root, object_file, output):
    baseline = root / "all-objects.o"
    if not baseline.is_file() or not object_file.is_file():
        raise ValueError("real upstream link gate object or pure-CRT object absent")
    revised = root / "with-pure-crt.o"
    subprocess.run(["ld", "-r", "-o", str(revised), str(baseline), str(object_file)],
                   check=True, timeout=30)
    old = symbols(cmd("nm", "-g", "-u", str(baseline)))
    new = symbols(cmd("nm", "-g", "-u", str(revised)))
    result = verify(old, new)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-objects", type=Path, required=True)
    parser.add_argument("--pure-crt-object", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    a=parser.parse_args()
    try:
        print(json.dumps(run(a.engine_objects,a.pure_crt_object,a.report),sort_keys=True))
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        print("PURE CRT LINK CHECK FAILED: "+str(e),file=sys.stderr)
        return 1

if __name__=="__main__":
    raise SystemExit(main())
