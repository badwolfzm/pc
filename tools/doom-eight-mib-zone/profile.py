#!/usr/bin/env python3
"""Run merged real Doom 16-provider bundle with 8 MiB allocation profile.

The only alteration is two exact 1MiB scalar constants in a generated,
isolated copy of the merged composition script. Do not edit shared sources.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MERGED = ROOT / "tools/doom-native-runtime-bundle/compose.py"
SECTION = ROOT / "tools/doom-thirteen-link-memory/integrate.py"
OLD_FLAG = '"-DDOOM_ARENA_BYTES=1048576"'
NEW_FLAG = '"-DDOOM_ARENA_BYTES=8388608"'
OLD_JSON = '"arena_bytes_reserved_in_BSS": 1048576'
NEW_JSON = '"arena_bytes_reserved_in_BSS": 8388608'
EXPECTED = 16
ZONE = 6 * 1024 * 1024
CAPACITY = 8 * 1024 * 1024


def checked_transform(original):
    if original.count(OLD_FLAG) != 1 or original.count(OLD_JSON) != 1:
        raise ValueError("merged bundle changed its arena profile; manual re-review required")
    if "__file__" not in original or "FORMAT_MERGED_COMMIT" not in original:
        raise ValueError("unknown canonical composition implementation")
    updated = original.replace(OLD_FLAG, NEW_FLAG).replace(OLD_JSON, NEW_JSON)
    if updated == original or updated.count(NEW_FLAG) != 1 or updated.count(NEW_JSON) != 1:
        raise ValueError("failed to enforce exact 8 MiB profile")
    return updated


def run(upstream, format_source, output):
    if not upstream.is_dir() or not format_source.is_file():
        raise ValueError("missing actual pinned Doom engine or selected formatter")
    output.mkdir(parents=True, exist_ok=True)
    generated = Path(__file__).with_name("_generated_compose.py")
    if generated.exists():
        raise ValueError("generated compose file already exists: refuses unexpected overwrite")
    canonical = MERGED.read_text()
    transformed = checked_transform(canonical)
    generated.write_text(transformed)
    try:
        subprocess.run([sys.executable, str(generated),
                        "--source", str(upstream),
                        "--format-source", str(format_source),
                        "--output", str(output / "bundle")],
                       check=True, timeout=180)
    finally:
        generated.unlink(missing_ok=True)
    report = json.loads((output / "bundle/report.json").read_text())
    if report.get("upstream_commit") != "e257a09527062bfb986b6e5e0780839b6cbd5b71":
        raise ValueError("unexpected upstream revision")
    if report.get("arena_bytes_reserved_in_BSS") != CAPACITY:
        raise ValueError("8 MiB arena was not integrated into real Doom")
    if len(report.get("exact_resolved_imports", [])) != EXPECTED:
        raise ValueError("16-provider canonical closure regressed")
    if report.get("original_undefined_count") != 37 or report.get("remaining_undefined_count") != 21:
        raise ValueError("unexpected real pinned source missing import inventory")
    if report.get("native_warden_boot_verified") is not False:
        raise ValueError("invalid native guest boot claim")
    if ZONE >= CAPACITY:
        raise ValueError("zone request exhausts the arena")

    spec = importlib.util.spec_from_file_location("elf_footprint", SECTION)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    linked = output / "bundle/genuine-doom-with-native-runtime.o"
    sections = mod.section_footprint(linked)
    if sections["allocatable_nobits_bytes"] < CAPACITY:
        raise ValueError("linked ELF does not reserve 8 MiB arena in BSS")
    result = {
        "status": "PASS",
        "upstream": report["upstream_commit"],
        "baseline_unresolved": report["original_undefined_count"],
        "remaining_unresolved": report["remaining_undefined_count"],
        "closed_exactly": report["exact_resolved_imports"],
        "dangerous_host_imports_still_unresolved": report["protected_host_calls_unresolved"],
        "default_doom_zone_bytes": ZONE,
        "compiled_bounded_arena_bytes": CAPACITY,
        "zone_minimum_fits_with_headroom": True,
        "candidate_static_elf_sections": sections,
        "merged_bundle_source_sha256": hashlib.sha256(canonical.encode()).hexdigest(),
        "combined_et_rel_sha256": hashlib.sha256(linked.read_bytes()).hexdigest(),
        "is_native_executable": False,
        "actual_doom_startup": False,
        "measured_guest_ram_bytes": None,
        "gameplay_fps": None,
        "description": "8 MiB candidate necessary capacity check, not a physical memory or game startup measurement",
    }
    (output / "report.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"baseline": result["baseline_unresolved"],
                      "remaining": result["remaining_unresolved"],
                      "zone_mib": 6, "arena_mib": 8,
                      "static_sections_bytes": sections["allocatable_total_section_bytes"]}))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", type=Path, required=True)
    p.add_argument("--format-source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.upstream, a.format_source, a.output)


if __name__ == "__main__":
    main()
