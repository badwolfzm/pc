#!/usr/bin/env python3
"""Fail-closed evidence validator for Warden's one-vCPU / 512 MiB target.

This validates submitted evidence, not guest instrumentation or game operation.
"""
import argparse
import json
import pathlib
import re
import sys

LIMIT = 512 * 1024 * 1024
COMPONENTS = ("kernel", "personality", "game_resident", "framebuffers",
              "page_tables", "pager_cache_metadata", "other")
SHA = re.compile(r"[0-9a-f]{64}\Z")

def is_int(value):
    return type(value) is int and value >= 0

def check(report):
    errors = []
    if not isinstance(report, dict) or report.get("schema") != 1:
        return ["invalid report schema"]
    if report.get("evidence") not in ("model", "native_warden_qemu"):
        errors.append("evidence must be model or native_warden_qemu")
    if report.get("claim") not in ("model_only", "guest_verified"):
        errors.append("invalid claim")
    if not isinstance(report.get("warden_commit"), str) or not re.fullmatch(r"[0-9a-f]{40}", report["warden_commit"]):
        errors.append("missing exact Warden commit")
    if not isinstance(report.get("game_sha256"), str) or not SHA.fullmatch(report["game_sha256"]):
        errors.append("missing game executable SHA256")
    guest = report.get("guest", {})
    if not isinstance(guest, dict):
        guest = {}
    ram, cpus = guest.get("physical_ram_bytes"), guest.get("vcpus")
    if not is_int(ram) or not 0 < ram <= LIMIT:
        errors.append("guest physical RAM exceeds 512 MiB or missing")
    if type(cpus) is not int or cpus != 1:
        errors.append("exactly one guest vCPU required")
    if guest.get("graphics") not in ("software", "integrated_gpu"):
        errors.append("graphics backend must be recorded")
    if guest.get("storage_authorized") is not True:
        errors.append("authorized storage provenance required")
    breakdown = guest.get("memory_components_bytes")
    if not isinstance(breakdown, dict) or set(breakdown) != set(COMPONENTS):
        errors.append("missing or overlapping memory accounting categories")
    elif not all(is_int(x) for x in breakdown.values()):
        errors.append("memory categories must be nonnegative integers")
    elif is_int(ram) and sum(breakdown.values()) > ram:
        errors.append("resident memory components exceed physical guest RAM")
    runs = report.get("runs")
    if not isinstance(runs, list):
        errors.append("missing runs")
        runs = []
    claim = report.get("claim")
    if claim == "guest_verified":
        if report.get("evidence") != "native_warden_qemu":
            errors.append("model data cannot support native guest verified claim")
        if len(runs) < 2:
            errors.append("two independent guest boots required")
        ids = set()
        for index, run in enumerate(runs):
            tag = f"run {index}"
            if not isinstance(run, dict):
                errors.append(f"{tag}: invalid run")
                continue
            boot_id = run.get("boot_id")
            if not isinstance(boot_id, str) or not boot_id or boot_id in ids:
                errors.append(f"{tag}: missing or duplicate boot id")
            else:
                ids.add(boot_id)
            if run.get("boot_ok") is not True or run.get("exit_ok") is not True:
                errors.append(f"{tag}: incomplete boot/exit")
            if run.get("input_and_state_advanced") is not True:
                errors.append(f"{tag}: no verified input/state progress")
            frames = run.get("frames")
            if not isinstance(frames, list) or len(frames) < 300:
                errors.append(f"{tag}: 300 genuine timestamped frames required")
                continue
            last = -1
            for frame in frames:
                if not isinstance(frame, dict):
                    errors.append(f"{tag}: malformed frame")
                    break
                t, digest = frame.get("timestamp_ns"), frame.get("digest")
                if not is_int(t) or t <= last or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{16}", digest):
                    errors.append(f"{tag}: invalid frame timestamp or digest")
                    break
                last = t
    return errors

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("report", type=pathlib.Path)
    args = p.parse_args()
    try:
        if args.report.stat().st_size > 4 * 1024 * 1024:
            raise ValueError("report exceeds size cap")
        report = json.loads(args.report.read_text(encoding="utf-8"))
        errors = check(report)
    except (OSError, ValueError, UnicodeError) as exc:
        errors = [str(exc)]
    print(json.dumps({"accepted": not errors, "errors": errors}, indent=2))
    return 1 if errors else 0

if __name__ == "__main__":
    sys.exit(main())
