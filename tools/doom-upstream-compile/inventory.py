#!/usr/bin/env python3
"""Build pinned upstream Doom engine objects and inventory unresolved host symbols."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
EXCLUDED = {"doomgeneric_soso.o"}
REQUIRED = {"doomgeneric.o", "d_main.o", "g_game.o", "w_wad.o", "z_zone.o"}

def object_list(makefile):
    line = re.search(r"(?m)^SRC_DOOM\s*=\s*(.+)$", makefile)
    if not line:
        raise ValueError("SRC_DOOM list absent")
    words = line.group(1).split()
    if len(words) != len(set(words)) or not REQUIRED.issubset(words) or any(not re.fullmatch(r"[A-Za-z0-9_]+\.o", x) for x in words):
        raise ValueError("invalid or incomplete object list")
    if not EXCLUDED.issubset(words):
        raise ValueError("expected platform backend absent")
    return [w[:-2] + ".c" for w in words if w not in EXCLUDED]

def zone_defaults(system_source):
    """Read the pinned engine's hard allocator defaults, without guessing."""
    values = {}
    for name in ("DEFAULT_RAM", "MIN_RAM"):
        match = re.search(r"(?m)^\\s*#\\s*define\\s+" + name + r"\\s+(\\d+)\\b", system_source)
        if match is None:
            raise ValueError("upstream allocator macro absent: " + name)
        value = int(match.group(1))
        if not 1 <= value <= 4096:
            raise ValueError("unreasonable upstream allocator macro " + name)
        values[name] = value
    return {
        "default_zone_bytes": values["DEFAULT_RAM"] * 1048576,
        "minimum_zone_bytes": values["MIN_RAM"] * 1048576,
        "default_zone_mib": values["DEFAULT_RAM"],
        "minimum_zone_mib": values["MIN_RAM"],
        "notice": "Source-level allocator default, not a measured resident RAM or guest address-space requirement; -mb override is possible but unproven."
    }

def nm_symbols(output):
    defs, unresolved = set(), set()
    for line in output.splitlines():
        m = re.fullmatch(r"\s*(?:(?:[0-9a-fA-F]+)\s+)?([A-Za-z])\s+(\S+)\s*", line)
        if not m:
            continue
        typ, symbol = m.groups()
        if typ == "U":
            unresolved.add(symbol)
        elif typ.upper() != "U" and typ.upper() not in {"N"}:
            defs.add(symbol)
    return defs, unresolved

def build(root, out, cc):
    root = root.resolve()
    makefile = (root / "Makefile.soso").read_text()
    files = object_list(makefile)
    out.mkdir(parents=True, exist_ok=True)
    built = []
    flags = ["-std=gnu11", "-O2", "-D_DEFAULT_SOURCE", "-DNORMALUNIX", "-DLINUX", "-DSNDSERV", "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200", "-fno-common"]
    for filename in files:
        src = root / filename
        if not src.is_file() or src.is_symlink():
            raise RuntimeError(f"missing source {filename}")
        obj = out / (filename[:-2] + ".o")
        subprocess.run([cc, *flags, "-c", str(src), "-o", str(obj)], check=True)
        built.append({"file": filename, "bytes": src.stat().st_size, "sha256": hashlib.sha256(src.read_bytes()).hexdigest(), "object": str(obj)})
    proc = subprocess.run(["nm", "-g", *[x["object"] for x in built]], check=True, capture_output=True, text=True)
    defs, needs = nm_symbols(proc.stdout)
    report = {
        "upstream": "ozkl/doomgeneric", "pinned_commit": PIN,
        "translation_units_compiled": len(built),
        "omitted_backend": sorted(EXCLUDED),
        "compiler": cc, "flags": flags, "files": built,
        "upstream_zone_allocator": zone_defaults((root / "i_system.c").read_text()),
        "unresolved_external_symbols": sorted(needs - defs),
        "notice": "Host object compilation only; no native Warden link/boot, no WAD, no FPS/RAM proof. Symbols may be resolved by a platform backend or libc."
    }
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--skip-revision-check", action="store_true", help="For offline fixtures only")
    args = parser.parse_args()
    try:
        if not args.skip_revision_check:
            rev = subprocess.run(["git", "-C", str(args.source), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
            if rev != PIN:
                raise ValueError(f"wrong upstream commit {rev}; requires {PIN}")
        report = build(args.source, args.output / "obj", args.cc)
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "inventory.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"Compiled {report['translation_units_compiled']} genuine upstream Doom source files; unresolved external symbol names: {len(report['unresolved_external_symbols'])}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, RuntimeError) as e:
        print(f"DOOM ENGINE COMPILATION FAILED: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
