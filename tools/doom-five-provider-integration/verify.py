#!/usr/bin/env python3
"""Validate genuine Doom freestanding ET_REL closure, never assert guest boot."""
import hashlib
import json
import pathlib
import subprocess
import sys

EXPECTED = {
    "__ctype_toupper_loc": "ctype",
    "__errno_location": "errno",
    "strtol": "strtol",
    "snprintf": "format",
    "vsnprintf": "format",
}
PROTECTED = {"system", "fopen", "fclose", "fread", "fwrite", "fseek",
             "ftell", "remove", "rename"}

def symbols(path, options):
    out = subprocess.check_output(["nm", "-P", "-g", *options, str(path)], text=True)
    result = {}
    for row in out.splitlines():
        fields = row.split()
        if len(fields) < 2:
            raise ValueError("bad nm record")
        name, kind = fields[:2]
        if name in result:
            raise ValueError("duplicate symbol record: " + name)
        result[name] = kind
    return result

def verify(before, after, providers, report_path):
    if set(providers) != set(EXPECTED.values()):
        raise ValueError("provider set is incomplete or unexpected")
    old = set(symbols(before, ["-u"]))
    new = set(symbols(after, ["-u"]))
    target = set(EXPECTED)
    if not target.issubset(old):
        raise ValueError("genuine base does not import all five symbols")
    if new != old - target:
        raise ValueError("unexpected symbol closure/new imports")
    if not PROTECTED.issubset(new):
        raise ValueError("protected host I/O/process unexpectedly resolved")
    exports = {}
    hashes = {}
    for label, path in providers.items():
        path = pathlib.Path(path)
        hashes[label] = hashlib.sha256(path.read_bytes()).hexdigest()
        defined = symbols(path, ["--defined-only"])
        undefined = set(symbols(path, ["-u"]))
        # Providers must not pull in arbitrary host dependencies. strtol is
        # allowed to depend on errno, which is supplied in this same link.
        if undefined - {"__errno_location"}:
            raise ValueError(label + " introduces external imports: " + repr(sorted(undefined)))
        for symbol, kind in defined.items():
            if kind.upper() == "U":
                continue
            if symbol in exports:
                raise ValueError("duplicate provider export: " + symbol)
            exports[symbol] = label
    for symbol, owner in EXPECTED.items():
        if exports.get(symbol) != owner:
            raise ValueError("incorrect/missing export owner for " + symbol)
    if set(exports) != target:
        raise ValueError("provider exports unexpected global symbols")
    result = {
        "native_executable_built": False,
        "native_warden_boot_verified": False,
        "fps_measured": False,
        "resident_ram_measured": False,
        "linked_artifact_type": "ET_REL",
        "resolved_exactly": sorted(target),
        "remaining_undefined": sorted(new),
        "original_undefined_count": len(old),
        "remaining_undefined_count": len(new),
        "provider_object_sha256": hashes,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(json.dumps(result, sort_keys=True))
    return result

if __name__ == "__main__":
    if len(sys.argv) != 8:
        raise SystemExit("usage: verify.py before.o after.o ctype.o errno.o strtol.o format.o result.json")
    verify(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]),
           dict(zip(("ctype", "errno", "strtol", "format"), map(pathlib.Path, sys.argv[3:7]))),
           pathlib.Path(sys.argv[7]))
