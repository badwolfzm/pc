#!/usr/bin/env python3
"""Compile real pinned Doom engine as FREESTANDING objects, then report native blockers.

Never labels relocatable .o compilation as a bootable Warden guest.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
ENGINE_OMIT = {"doomgeneric_soso.o", "w_file_stdc.o"}
COMPONENTS = [
    "tools/doomgeneric-warden/doomgeneric_warden.c",
    "tools/doom-wad-stream/wad_stream.c",
    "tools/doom-wad-class-bridge/doom_wad_bridge.c",
    "tools/doom-real-wfile-link/bridge_backend.c",
    "tools/doom-mini-crt/mini_string.c",
]
CFLAGS = [
    "-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-pic", "-fno-pie",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200",
    "-D_DEFAULT_SOURCE", "-DNORMALUNIX", "-DLINUX", "-DSNDSERV",
]
HOST_IO = {"fopen", "fclose", "fread", "fwrite", "fseek", "ftell",
           "fprintf", "printf", "perror", "puts", "remove", "rename",
           "stat", "fstat", "opendir", "readdir", "system", "popen",
           "execve", "fork", "socket", "connect"}
ALLOC = {"malloc", "calloc", "realloc", "free", "aligned_alloc"}
MATH = {"sin", "cos", "tan", "atan2", "sqrt", "pow", "floor", "ceil", "fabs"}


def missing_symbols(nm_output):
    result = set()
    for line in nm_output.splitlines():
        match = re.fullmatch(r"\s*U\s+(\S+)\s*", line)
        if match:
            result.add(match.group(1))
        elif " U " in line or line.lstrip().startswith("U "):
            raise ValueError("unexpected unresolved symbol line: " + line)
    return sorted(result)


def classify(symbols):
    missing = set(symbols)
    return {
        "host_file_process_network_calls": sorted(missing & HOST_IO),
        "allocation_calls": sorted(missing & ALLOC),
        "math_calls": sorted(missing & MATH),
        "other": sorted(missing - HOST_IO - ALLOC - MATH),
    }


def execute(args, cwd=None):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("command failed (" + " ".join(map(str, args[:6])) +
                           "):\n" + result.stderr[-6000:])
    return result.stdout


def compile_real_engine(source, output):
    spec = importlib.util.spec_from_file_location(
        "doom_object_list", ROOT / "tools/doom-upstream-compile/inventory.py")
    upstream = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(upstream)
    real_files = upstream.object_list((source / "Makefile.soso").read_text())
    selected = [file for file in real_files if file[:-2] + ".o" not in ENGINE_OMIT]
    if len(selected) != 79 or any(not (source / file).is_file() for file in selected):
        raise ValueError("unexpected pinned engine source list")
    incl = [
        "-I" + str(source),
        "-I" + str(ROOT / "tools/doom-wad-stream"),
        "-I" + str(ROOT / "tools/doom-wad-class-bridge"),
    ]
    objects = []
    files = [(source / file, "upstream_" + file[:-2] + ".o")
             for file in selected]
    files += [(ROOT / file, "warden_" + pathlib.Path(file).stem + ".o")
              for file in COMPONENTS]
    for src, name in files:
        if not src.is_file():
            raise ValueError(f"missing component: {src}")
        obj = output / name
        execute(["cc", *CFLAGS, *incl, "-c", str(src), "-o", str(obj)])
        objects.append(str(obj))
    execute(["ld", "-r", "-o", str(output / "all-objects.o"), *objects])
    return selected, files


def run(source, output):
    source = source.resolve()
    revision = execute(["git", "-C", str(source), "rev-parse", "HEAD"]).strip()
    if revision != PIN:
        raise ValueError("upstream revision mismatch: " + revision)
    output.mkdir(parents=True, exist_ok=True)
    selected, files = compile_real_engine(source, output)
    combined = output / "all-objects.o"
    undefined = missing_symbols(execute(["nm", "-g", "-u", str(combined)]))
    symbols = classify(undefined)
    result = {
        "upstream_commit": PIN,
        "real_upstream_source_count": len(selected),
        "upstream_excluded_backends": sorted(ENGINE_OMIT),
        "warden_component_count": len(COMPONENTS),
        "build_mode": "x86_64 relocatable freestanding objects; not an executable",
        "compiler_flags": CFLAGS,
        "combined_object_sha256": hashlib.sha256(combined.read_bytes()).hexdigest(),
        "unresolved_symbol_count": len(undefined),
        "unresolved_symbols": undefined,
        "unresolved_categories": symbols,
        "native_executable_built": False,
        "native_warden_boot_verified": False,
        "guest_total_ram_mib_measured": None,
        "status": "BLOCKED: runtime/allocator/entrypoint/ABI/guest memory mapping not integrated",
        "required_next_steps": [
            "Provide audited freestanding replacements for every imported service",
            "Supply a legal capability-authorized immutable WAD source",
            "Link at the actual Warden ring-3 virtual addresses and validate W^X",
            "Measure full ELF image, .bss, heap, game, framebuffer and kernel budgets",
            "Boot real engine twice in QEMU -smp 1 -m 512M with evidence",
        ],
    }
    (output / "gate.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=pathlib.Path, required=True)
    p.add_argument("--output", type=pathlib.Path, required=True)
    a = p.parse_args()
    try:
        result = run(a.source, a.output)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as e:
        p.error(str(e))
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
