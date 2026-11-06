#!/usr/bin/env python3
"""Require two independent native Warden guest boots in the same 512 MiB profile.

Validates independent boot outcomes; does NOT equate image hashes (timing
and injected input can legitimately alter frames between boots).
"""
import argparse
import json
from pathlib import Path
import sys

class Invalid(ValueError):
    pass

def check_two(first, second):
    if first is second or not isinstance(first, dict) or not isinstance(second, dict):
        raise Invalid("two separate evidence reports required")
    fields = ("evidence_kind", "guest_ram_configured_bytes", "guest_vcpus_configured", "kernel_checks_passed")
    required = {
        "evidence_kind": "native-warden-pong-qemu-serial",
        "guest_ram_configured_bytes": 512 * 1024 * 1024,
        "guest_vcpus_configured": 1,
        "kernel_checks_passed": 15
    }
    for label, sample in (("boot1", first), ("boot2", second)):
        for field in fields:
            if sample.get(field) != required[field] or type(sample.get(field)) != type(required[field]):
                raise Invalid(label + ": invalid " + field)
        for key in ("paced_phase_frames", "allocator_frames_in_use_at_end", "allocator_frame_bytes_at_end"):
            if type(sample.get(key)) is not int or sample[key] <= 0:
                raise Invalid(label + ": missing or invalid " + key)
        if sample["allocator_frame_bytes_at_end"] != sample["allocator_frames_in_use_at_end"] * 4096:
            raise Invalid(label + ": mismatched frame count/bytes")
        rate = sample.get("paced_phase_fps_reported")
        if type(rate) not in (float, int) or not (rate > 0 and rate < 1000000):
            raise Invalid(label + ": invalid paced frame rate")
        image_hash = sample.get("screen_hash")
        if not isinstance(image_hash, str) or len(image_hash) < 4 or not image_hash.startswith("0x"):
            raise Invalid(label + ": missing screen hash")
    return {
        "two_native_boots_passed": True,
        "configured_guest_ram_bytes": required["guest_ram_configured_bytes"],
        "configured_guest_vcpus": 1,
        "reported_paced_fps": [first["paced_phase_fps_reported"], second["paced_phase_fps_reported"]],
        "reported_paced_frames": [first["paced_phase_frames"], second["paced_phase_frames"]],
        "reported_allocator_frames_at_end": [
            first["allocator_frames_in_use_at_end"], second["allocator_frames_in_use_at_end"]
        ],
        "frame_hashes": [first["screen_hash"], second["screen_hash"]],
        "caveat": "Two independent serial-evidence reports are not a cryptographic runtime proof or total physical RAM accounting. Real native Pong only; no modern PC game benchmark."
    }

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("boot1", type=Path)
    p.add_argument("boot2", type=Path)
    p.add_argument("summary", type=Path)
    a = p.parse_args(argv)
    if a.boot1.resolve() == a.boot2.resolve():
        print("REPEAT-BOOT CHECK FAILED: same evidence file supplied twice", file=sys.stderr)
        return 1
    try:
        first = json.loads(a.boot1.read_text())
        second = json.loads(a.boot2.read_text())
        result = check_two(first, second)
        a.summary.parent.mkdir(parents=True, exist_ok=True)
        a.summary.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(json.dumps(result, sort_keys=True))
    except (OSError, ValueError, Invalid, UnicodeError) as exc:
        print("REPEAT-BOOT CHECK FAILED:", exc, file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
