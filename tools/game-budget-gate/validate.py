#!/usr/bin/env python3
"""Fail-closed validator for *reported* Warden G0 run evidence (not a benchmark)."""
import argparse
import json
import math
from pathlib import Path

MAX_BYTES = 1024 * 1024
REQUIRED = {"target", "qemu", "limits", "runs"}

def positive_int(value):
    return type(value) is int and 0 < value <= 2**63 - 1

def nonnegative_int(value):
    return type(value) is int and 0 <= value <= 2**63 - 1

def finite_positive_milliseconds(value):
    if type(value) not in (int, float) or value <= 0:
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False

def validate(doc):
    errors = []
    def check(condition, message):
        if not condition:
            errors.append(message)
    if not isinstance(doc, dict):
        return ["root must be an object"]
    check(REQUIRED <= doc.keys(), "missing top-level keys")
    target = doc.get("target")
    target = target if isinstance(target, dict) else {}
    check(target.get("platform") == "warden-qemu", "platform must be warden-qemu")
    check(target.get("game_processes") == 1, "exactly one game process required")
    check(type(target.get("game_processes")) is int, "game_processes must be integer")
    qemu = doc.get("qemu")
    qemu = qemu if isinstance(qemu, dict) else {}
    check(qemu.get("vcpus") == 1 and type(qemu.get("vcpus")) is int, "require exactly one vCPU")
    check(qemu.get("mode") in ("tcg", "kvm", "whpx"), "explicit emulation mode required")
    check(isinstance(qemu.get("cpu"), str) and bool(qemu.get("cpu")), "CPU model required")
    check(isinstance(qemu.get("version"), str) and bool(qemu.get("version")), "QEMU version required")
    limits = doc.get("limits")
    limits = limits if isinstance(limits, dict) else {}
    for name in ("guest_ram_cap_bytes", "host_rss_cap_bytes"):
        check(positive_int(limits.get(name)), name + " must be positive integer")
    runs = doc.get("runs")
    if not isinstance(runs, list) or len(runs) != 2:
        return errors + ["exactly two independent runs required"]
    hashes = []
    for i, run in enumerate(runs):
        prefix = "run " + str(i + 1) + ": "
        if not isinstance(run, dict):
            errors.append(prefix + "must be object")
            continue
        for name in ("guest_peak_bytes", "host_peak_rss_bytes"):
            value = run.get(name)
            cap = limits.get("guest_ram_cap_bytes" if name == "guest_peak_bytes" else "host_rss_cap_bytes")
            check(positive_int(value), prefix + name + " must be positive integer")
            if positive_int(value) and positive_int(cap):
                check(value <= cap, prefix + name + " exceeds cap")
        check(run.get("boot_ok") is True, prefix + "boot must succeed")
        check(run.get("clean_exit") is True, prefix + "clean exit must succeed")
        check(run.get("warmup_frames") is not None and nonnegative_int(run.get("warmup_frames")), prefix + "warmup count required")
        frames = run.get("timed_frames")
        check(positive_int(frames) and frames >= 300, prefix + "need >=300 timed frames")
        times = run.get("frametimes_ms")
        if not isinstance(times, list) or not positive_int(frames) or len(times) != frames or frames > 100000:
            errors.append(prefix + "frametimes must match timed_frames (max 100000)")
        else:
            check(all(finite_positive_milliseconds(t) for t in times),
                  prefix + "frametimes must be finite positive milliseconds")
        h = run.get("frame_digest")
        check(isinstance(h, str) and len(h) == 16 and all(c in "0123456789abcdef" for c in h),
              prefix + "digest must be 16 lowercase hex characters")
        hashes.append(h)
    if len(hashes) == 2 and all(isinstance(h, str) and len(h) == 16 for h in hashes):
        check(hashes[0] == hashes[1], "two replay digests differ")
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    try:
        if args.evidence.stat().st_size > MAX_BYTES:
            raise ValueError("evidence larger than 1 MiB")
        doc = json.loads(args.evidence.read_text(encoding="utf-8"), parse_constant=lambda x: (_ for _ in ()).throw(ValueError("non-finite JSON: " + x)))
        errors = validate(doc)
    except (OSError, UnicodeError, ValueError, RecursionError) as exc:
        errors = [str(exc)]
    if errors:
        for error in errors:
            print("FAIL:", error)
        return 1
    print("PASS: G0 evidence is internally consistent (reported measurements only; not independently verified)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
