#!/usr/bin/env python3
"""Real Doom linked-object contract: exactly three independent providers."""
import argparse
import hashlib
import json
import pathlib
import subprocess

EXPECTED = frozenset({"__ctype_toupper_loc", "__errno_location", "strtol"})
PROTECTED = frozenset({"system", "fopen", "fclose", "fread", "fwrite", "fseek",
                       "ftell", "remove", "rename"})
OWNERS = {"ctype": "__ctype_toupper_loc",
          "errno": "__errno_location",
          "strtol": "strtol"}


def symbols(path):
    result = subprocess.run(["nm", "-P", "-g", str(path)], check=True,
                            capture_output=True, text=True)
    definitions, undefined = set(), set()
    for line in result.stdout.splitlines():
        words = line.split()
        if len(words) < 2:
            raise ValueError("invalid nm line: " + line)
        name, typ = words[:2]
        if typ == "U":
            if name in undefined:
                raise ValueError("duplicate unresolved symbol")
            undefined.add(name)
        elif typ in {"T", "D", "B", "R", "C", "W", "V"}:
            if name in definitions:
                raise ValueError("duplicate definition")
            definitions.add(name)
        elif typ in {"t", "d", "b", "r"}:
            continue
        else:
            raise ValueError("unknown nm symbol type: " + line)
    return definitions, undefined


def verify(before, after, providers):
    if set(providers) != set(OWNERS):
        raise ValueError("missing provider identity")
    provider_defs = []
    for key, name in OWNERS.items():
        defs, missing = symbols(providers[key])
        if defs != {name}:
            raise ValueError("provider owns unexpected or duplicate symbol: " + key)
        if missing != ({"__errno_location"} if key == "strtol" else set()):
            raise ValueError("provider imports changed unexpectedly: " + key)
        provider_defs.append(name)
    if len(set(provider_defs)) != 3:
        raise ValueError("duplicate provider")
    original_defs, original = symbols(before)
    _, remaining = symbols(after)
    if EXPECTED & original_defs:
        raise ValueError("baseline engine already defines provider")
    if not EXPECTED.issubset(original):
        raise ValueError("expected three missing symbols absent in baseline")
    if remaining != original - EXPECTED:
        raise ValueError("unexpected change to genuine Doom imports")
    if not PROTECTED.issubset(remaining):
        raise ValueError("protected host process/file imports were masked")
    return {
        "original_unresolved": len(original),
        "remaining_unresolved": len(remaining),
        "closed_exactly": sorted(EXPECTED),
        "still_unresolved": sorted(remaining),
        "before_sha256": hashlib.sha256(before.read_bytes()).hexdigest(),
        "after_sha256": hashlib.sha256(after.read_bytes()).hexdigest(),
        "providers_sha256": {key: hashlib.sha256(path.read_bytes()).hexdigest()
                             for key, path in providers.items()},
        "real_guest_executable": False,
        "native_warden_doom_boot": False,
        "guest_memory_mib_measured": None,
        "fps_measured": None,
        "scope": "one freestanding ET_REL; static single-thread errno only",
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "after", "ctype", "errno", "strtol", "output"):
        p.add_argument(name, type=pathlib.Path)
    a = p.parse_args()
    report = verify(a.before, a.after,
                    {"ctype": a.ctype, "errno": a.errno, "strtol": a.strtol})
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
