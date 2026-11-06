#!/usr/bin/env python3
"""Source-backed, relocatable heap-ABI closure only; NOT Doom native boot."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ARENA_SHA = "315cc925ebf67f67d883bfa4d507ebcbb6b84c10"
ALLOCS = {"malloc", "calloc", "realloc", "free"}
GATE = ROOT / "tools/doom-native-link-gate/gate.py"


def load_gate():
    spec = importlib.util.spec_from_file_location("warden_doom_native_gate", GATE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def closure(before, after):
    before, after = set(before), set(after)
    missing = ALLOCS - before
    if missing:
        raise ValueError("base real-engine binary must import all four allocators: " + repr(sorted(missing)))
    if after & ALLOCS:
        raise ValueError("allocation imports remained unresolved: " + repr(sorted(after & ALLOCS)))
    added = after - before
    if added:
        raise ValueError("heap integration introduced new external requirements: " + repr(sorted(added)))
    return sorted(before - after)


def run(source, output, arena_bytes=1048576):
    if arena_bytes not in (1048576, 4194304):
        raise ValueError("only tested 1 MiB and 4 MiB arena configurations allowed")
    if not (ROOT / "tools/doom-freestanding-arena/arena.c").is_file():
        raise ValueError("dependency PR #113 arena.c not staged")
    output.mkdir(parents=True, exist_ok=True)
    gate = load_gate()
    baseline = gate.run(source, output / "base")
    imports = baseline["unresolved_symbols"]
    common = [
        "cc", *gate.CFLAGS, "-DDOOM_ARENA_BYTES=" + str(arena_bytes),
        "-I" + str(ROOT / "tools/doom-freestanding-arena"),
        "-c",
    ]
    heap = output / "heap.o"
    wrapper = output / "heap_alias.o"
    gate.execute([*common, str(ROOT / "tools/doom-freestanding-arena/arena.c"), "-o", str(heap)])
    gate.execute([*common, str(ROOT / "tools/doom-native-heap-closure/heap_alias.c"), "-o", str(wrapper)])
    linked = output / "engine-with-heap.o"
    gate.execute(["ld", "-r", "-o", str(linked), str(output / "base/all-objects.o"), str(heap), str(wrapper)])
    remaining = gate.missing_symbols(gate.execute(["nm", "-g", "-u", str(linked)]))
    removed = closure(imports, remaining)
    result = {
        "evidence": "real upstream Doom + real bounded allocator, x86-64 relocatable freestanding object",
        "upstream_commit": gate.PIN,
        "arena_dependency_pr": 113,
        "arena_dependency_sha": ARENA_SHA,
        "arena_capacity_bytes": arena_bytes,
        "original_undefined_count": len(imports),
        "remaining_undefined_count": len(remaining),
        "resolved_symbols": removed,
        "still_unresolved": remaining,
        "combined_object_sha256": hashlib.sha256(linked.read_bytes()).hexdigest(),
        "native_executable_built": False,
        "warden_qemu_game_boot_verified": False,
        "whole_guest_ram_512m_verified": False,
        "warning": "Single-thread prototype heap. 1 MiB is not an observed game requirement. No host-IO/allocator provenance or guest runtime permissions are implied."
    }
    (output / "heap-closure.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--arena-bytes", type=int, choices=[1048576, 4194304], default=1048576)
    a = ap.parse_args()
    try:
        print(json.dumps(run(a.source, a.output, a.arena_bytes), indent=2, sort_keys=True))
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        ap.error(str(exc))


if __name__ == "__main__":
    main()
