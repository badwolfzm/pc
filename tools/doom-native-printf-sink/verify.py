#!/usr/bin/env python3
"""Verify genuine pinned Doom one-symbol printf closure without host I/O stubs."""
import json
import pathlib
import subprocess
import sys

PROTECTED = {"system", "fopen", "fclose", "fread", "fwrite", "fseek",
             "ftell", "remove", "rename"}
def nm(path, defined=False):
    cmd = ["nm", "-P", "-g", "--defined-only" if defined else "-u", str(path)]
    p = subprocess.run(cmd, check=True, text=True, capture_output=True)
    out = {}
    for line in p.stdout.splitlines():
        values = line.split()
        if len(values) < 2 or (not defined and values[1] != "U"):
            raise ValueError("bad nm record " + line)
        if values[0] in out:
            raise ValueError("duplicate nm symbol " + values[0])
        out[values[0]] = values[1]
    return out

def verify(before, after, provider, report):
    original = set(nm(before))
    remaining = set(nm(after))
    exports = set(nm(provider, defined=True))
    imported = set(nm(provider))
    if "printf" not in original:
        raise ValueError("genuine engine must import printf")
    if remaining != original - {"printf"}:
        raise ValueError("unexpected added/removed symbol in combined link")
    if not PROTECTED.issubset(remaining):
        raise ValueError("protected host I/O/process import lost")
    if exports != {"printf", "warden_printf_bind"} or imported != {"vsnprintf"}:
        raise ValueError("incorrect provider ABI or new host imports")
    result = {
        "resolved_exactly": ["printf"],
        "original_missing": len(original),
        "remaining_missing": len(remaining),
        "protected_host_imports": sorted(PROTECTED),
        "guest_callback_installed": False,
        "final_executable_built": False,
        "native_warden_boot_verified": False,
        "memory_fps_measured": False
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, indent=2, sort_keys=True)+"\n")
    print(json.dumps(result, sort_keys=True))
    return result

if __name__ == "__main__":
    if len(sys.argv) != 5: raise SystemExit("verify.py before.o after.o printf.o report.json")
    verify(*(pathlib.Path(s) for s in sys.argv[1:]))
