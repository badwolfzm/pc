#!/usr/bin/env python3
"""Verify exactly one original Doom host 'exit' import is safely closed."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

TARGET = "exit"
PROTECTED = frozenset(("system", "fopen", "fclose", "fread", "fwrite",
                      "fseek", "ftell", "remove", "rename"))


def symbols(path):
    stdout = subprocess.run(["nm", "-P", "-g", str(path)], check=True,
                            capture_output=True, text=True).stdout
    defined, undefined = set(), set()
    for row in stdout.splitlines():
        fields = row.split()
        if len(fields) < 2 or len(fields) > 4:
            raise ValueError("bad nm record: " + row)
        symbol, typ = fields[:2]
        if typ == "U":
            if symbol in undefined: raise ValueError("duplicate unresolved import")
            undefined.add(symbol)
        elif typ in ("T", "D", "B", "R", "C", "A", "W", "V"):
            if symbol in defined: raise ValueError("duplicate global definition")
            defined.add(symbol)
        else:
            raise ValueError("unknown nm record: " + row)
    return defined, undefined


def check(before, after, provider):
    providers, provider_missing = symbols(provider)
    if providers != {"exit"} or provider_missing:
        raise ValueError("exit bridge must export only exit, zero external imports")
    b_defined, b = symbols(before)
    _, a = symbols(after)
    if TARGET in b_defined or TARGET not in b:
        raise ValueError("baseline must genuinely lack exit")
    if a != b - {TARGET}:
        raise ValueError("unexpected resolved or added import")
    if not PROTECTED.issubset(a):
        raise ValueError("host file/process imports illegally resolved")
    return {
        "actual_pinned_doom_et_rel_link": True,
        "closed_exactly": ["exit"],
        "baseline_unresolved": len(b),
        "remaining_unresolved": len(a),
        "remaining_symbols": sorted(a),
        "combined_sha256": hashlib.sha256(Path(after).read_bytes()).hexdigest(),
        "provider_sha256": hashlib.sha256(Path(provider).read_bytes()).hexdigest(),
        "exit_abi": "Warden SYS_EXIT (6), current thread, no capability arguments",
        "host_machine_shutdown": False,
        "supports_atexit_or_stdio_flush": False,
        "native_game_booted": False,
        "guest_ram_high_water_measured": None,
        "fps_measured": None
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for k in ("before", "after", "provider", "report"):
        p.add_argument(k, type=Path)
    a = p.parse_args()
    result = check(a.before, a.after, a.provider)
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
