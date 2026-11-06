#!/usr/bin/env python3
"""Reproducible *real engine* freestanding ELF partial-link size audit; not a boot."""
import argparse
import json
import pathlib
import re
import subprocess
import sys

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
NATIVE_WINDOW_BYTES = 0xA00000 - 0x800000
NATIVE_DATA_HEAP_BYTES = 0x9E0000 - 0x840000
CFLAGS = ("-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin",
          "-fno-stack-protector", "-fno-pie",
          "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200")
REQUIRED = {"doomgeneric.o", "d_main.o", "g_game.o", "w_wad.o", "z_zone.o"}
SECTION = re.compile(
    r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+"
    r"[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+"
    r"[0-9a-fA-F]+\s+([A-Z]+)\s+\d+\s+\d+\s+\d+\s*$"
)
def source_names(text):
    m = re.search(r"(?m)^SRC_DOOM\s*=\s*(.+)$", text)
    if not m:
        raise ValueError("upstream source manifest missing")
    names = m.group(1).split()
    if (len(names) < 70 or len(names) > 150 or len(names) != len(set(names))
            or "doomgeneric_soso.o" not in names or not REQUIRED.issubset(names)
            or any(re.fullmatch(r"[A-Za-z0-9_]+\.o", x) is None for x in names)):
        raise ValueError("invalid source object manifest")
    return [name[:-2] + ".c" for name in names if name != "doomgeneric_soso.o"]

def sections(report):
    result = []
    for row in report.splitlines():
        m = SECTION.fullmatch(row)
        if not m:
            continue
        name, kind, amount, flags = m.groups()
        if "A" not in flags:
            continue
        size = int(amount, 16)
        result.append({"name": name, "type": kind, "size_bytes": size,
                       "writable": "W" in flags, "executable": "X" in flags})
    if not result or not any(s["executable"] and s["size_bytes"] for s in result):
        raise ValueError("missing executable allocatable sections")
    return result

def unresolved(text):
    result = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) > 1 and parts[1] == "U":
            result.add(parts[0])
    return sorted(result)

def evaluate(sec, extern):
    file_backed = sum(s["size_bytes"] for s in sec if s["type"] != "NOBITS")
    zero_fill = sum(s["size_bytes"] for s in sec if s["type"] == "NOBITS")
    writable = sum(s["size_bytes"] for s in sec if s["writable"])
    executable = sum(s["size_bytes"] for s in sec if s["executable"])
    total = file_backed + zero_fill
    return {
        "allocatable_file_backed_bytes": file_backed,
        "allocatable_zero_fill_bytes": zero_fill,
        "allocatable_section_bytes": total,
        "writable_section_bytes": writable,
        "executable_section_bytes": executable,
        "upstream_engine_unresolved_symbols": extern,
        "upstream_engine_unresolved_count": len(extern),
        "existing_native_pong_window_bytes": NATIVE_WINDOW_BYTES,
        "existing_native_pong_data_heap_window_bytes": NATIVE_DATA_HEAP_BYTES,
        "raw_section_sum_exceeds_native_pong_window": total > NATIVE_WINDOW_BYTES,
        "raw_writable_sections_exceed_native_pong_data_heap_window": writable > NATIVE_DATA_HEAP_BYTES,
        "section_details": sec,
        "limits": "Unplaced ET_REL upstream ENGINE ONLY. No real guest linker script, alignment/page mapping, stack/heap, Doomgeneric Warden callback/BSS, WAD, runtime libc, kernel, QEMU boot, physical RAM peak or FPS. A raw section sum fitting a window does NOT prove an executable fits."
    }

def run(source, output, compiler):
    source = source.resolve(strict=True)
    sha = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"],
                                  text=True, timeout=15).strip()
    if sha != PIN:
        raise ValueError("unexpected upstream commit " + sha)
    names = source_names((source / "Makefile.soso").read_text())
    output.mkdir(parents=True, exist_ok=True)
    objs = []
    for name in names:
        path = source / name
        if not path.is_file() or path.is_symlink():
            raise ValueError("source missing or symlinked: " + name)
        obj = output / (name[:-2] + ".o")
        subprocess.run([compiler, *CFLAGS, "-c", str(path), "-o", str(obj)],
                       check=True, timeout=40)
        objs.append(str(obj))
    combined = output / "upstream-engine-only.o"
    subprocess.run([compiler, "-nostdlib", "-r", *objs, "-o", str(combined)],
                   check=True, timeout=40)
    se = subprocess.run(["readelf", "-W", "-S", str(combined)],
                        text=True, capture_output=True, check=True, timeout=15)
    nm = subprocess.run(["nm", "-P", "-g", "-u", str(combined)],
                        text=True, capture_output=True, check=True, timeout=15)
    report = evaluate(sections(se.stdout), unresolved(nm.stdout))
    report.update({"pinned_commit": PIN, "upstream_source_count": len(names),
                   "build_flags": list(CFLAGS), "link_mode": "ELF64 ET_REL engine-only; partial link, not native Warden guest ELF"})
    (output / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({k: report[k] for k in ("upstream_source_count", "allocatable_section_bytes",
        "writable_section_bytes", "upstream_engine_unresolved_count",
        "raw_section_sum_exceeds_native_pong_window")}, sort_keys=True))
    return report

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=pathlib.Path)
    p.add_argument("--out", required=True, type=pathlib.Path)
    p.add_argument("--compiler", default="cc")
    args = p.parse_args()
    try:
        run(args.source, args.out, args.compiler)
    except (ValueError, OSError, subprocess.SubprocessError) as e:
        print("NATIVE PARTIAL LINK AUDIT FAILED:", e, file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
