#!/usr/bin/env python3
"""Reproducible compilation DIAGNOSTIC for genuine upstream Doom source; not a guest link."""
import argparse
import json
import pathlib
import re
import subprocess
import sys

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
REQUIRED = {"doomgeneric.o", "d_main.o", "g_game.o", "w_wad.o", "z_zone.o"}
FLAGS = ("-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin",
         "-fno-stack-protector", "-fno-pie",
         "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200")

def source_names(makefile):
    match = re.search(r"(?m)^SRC_DOOM\s*=\s*(.+)$", makefile)
    if not match:
        raise ValueError("missing SRC_DOOM source manifest")
    words = match.group(1).split()
    if (not REQUIRED.issubset(words) or "doomgeneric_soso.o" not in words or
        len(words) != len(set(words)) or len(words) > 150 or
        any(not re.fullmatch(r"[A-Za-z0-9_]+\.o", word) for word in words)):
        raise ValueError("invalid upstream object list")
    return [name[:-2] + ".c" for name in words if name != "doomgeneric_soso.o"]

def parse_undefined(text):
    found = set()
    for line in text.splitlines():
        # POSIX nm -P format: symbol TYPE VALUE [SIZE]
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "U":
            found.add(parts[0])
    return sorted(found)

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", required=True, type=pathlib.Path)
    p.add_argument("--out", required=True, type=pathlib.Path)
    p.add_argument("--compiler", default="cc")
    p.add_argument("--register-policy", choices=("general-only", "systemv"), default="general-only")
    p.add_argument("--strict-compile", action="store_true",
                   help="Exit nonzero if any genuine upstream unit fails compilation")
    args = p.parse_args(argv)
    try:
        src = args.upstream.resolve(strict=True)
        commit = subprocess.run(["git", "-C", str(src), "rev-parse", "HEAD"],
                                capture_output=True, text=True, check=True, timeout=10).stdout.strip()
        if commit != PIN:
            raise ValueError("upstream SHA mismatch: " + commit)
        names = source_names((src / "Makefile.soso").read_text())
        args.out.mkdir(parents=True, exist_ok=True)
        records = []
        objects = []
        flags = [*FLAGS, *(["-mgeneral-regs-only"] if args.register_policy == "general-only" else [])]
        for name in names:
            path = src / name
            if not path.is_file() or path.is_symlink():
                raise ValueError("missing or symlinked upstream source: " + name)
            obj = args.out / (name[:-2] + ".o")
            command = [args.compiler, *flags, "-c", str(path), "-o", str(obj)]
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                entry = {"source": name, "success": result.returncode == 0,
                         "exit_code": result.returncode,
                         "diagnostic": (result.stderr + result.stdout)[-10000:]}
            except subprocess.TimeoutExpired:
                entry = {"source": name, "success": False, "exit_code": None,
                         "diagnostic": "compiler timed out after 30 seconds"}
            records.append(entry)
            if entry["success"]:
                objects.append(str(obj))
        externals = []
        if objects:
            nm = subprocess.run(["nm", "-P", "-g", *objects],
                                capture_output=True, text=True, timeout=30)
            if nm.returncode:
                raise ValueError("nm failed: " + nm.stderr[:500])
            defined = {parts[0] for line in nm.stdout.splitlines() if (parts := line.split()) and len(parts) >= 2 and parts[1].upper() in ("T", "D", "B", "R", "A", "C", "W", "V")}
            externals = sorted(set(parse_undefined(nm.stdout)) - defined)
        failed = [x["source"] for x in records if not x["success"]]
        report = {"source": "ozkl/doomgeneric", "pinned_commit": PIN,
                  "scope": "freestanding compiler probe only; not a native ELF link, Warden boot or playable Doom",
                  "compile_flags": flags, "register_policy": args.register_policy, "total": len(names),
                  "compiled": len(objects), "failed": len(failed),
                  "failed_sources": failed, "unresolved_symbols_from_compiled_subset": externals,
                  "external_symbols_partial": bool(failed),
                  "results": records}
        (args.out / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"Upstream freestanding compile: {len(objects)}/{len(names)} files; {len(externals)} distinct external names; report={args.out / 'report.json'}")
        return 1 if args.strict_compile and failed else 0
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print("PROBE ERROR: " + str(exc), file=sys.stderr)
        return 2

if __name__ == "__main__":
    sys.exit(main())
