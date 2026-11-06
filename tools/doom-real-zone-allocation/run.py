#!/usr/bin/env python3
"""Compile the repository's REAL arena.c at 1MiB and 8MiB and execute each."""
from pathlib import Path
import subprocess
import sys
import json
import os

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ARENA = ROOT / "tools/doom-freestanding-arena"

def execute(argv):
    subprocess.run(argv, check=True, cwd=ROOT)

def main():
    out = ROOT / "target/doom-real-zone-allocation"
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for capacity, success in ((1048576, False), (8388608, True)):
        executable = out / ("arena-" + str(capacity))
        cmd = [
            "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fno-builtin", "-fno-omit-frame-pointer",
            "-fsanitize=undefined", "-fno-sanitize-recover=all",
            "-DDOOM_ARENA_BYTES=" + str(capacity),
            "-DZONE_EXPECT_SUCCESS=" + str(int(success)),
            "-I" + str(ARENA), str(ARENA / "arena.c"),
            str(HERE / "zone_allocation.c"), "-o", str(executable)
        ]
        execute(cmd)
        execute([str(executable)])
        results.append({
            "arena_capacity_bytes": capacity,
            "doom_default_zone_bytes": 6*1024*1024,
            "expected_zone_alloc_success": success,
            "host_test_passed": True,
            "native_warden_boot": False,
        })
    print(json.dumps({"kind": "host actual arena.c exercise, NOT Doom guest boot",
                      "results": results}, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
