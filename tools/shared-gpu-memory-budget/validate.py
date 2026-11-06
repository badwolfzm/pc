#!/usr/bin/env python3
"""Validate a self-reported one-core/512 MiB Warden memory envelope.

Host-side accounting only; not telemetry or proof that any game runs.
All bucket numbers represent mutually exclusive *resident physical* MiB.
The GPU's shared allocation is a subset of graphics_mib, not another bucket.
"""
import json
import math
import sys

BUCKETS = ("kernel_mib", "game_mib", "personality_mib", "graphics_mib",
           "paging_mib", "other_mib")
FIELDS = BUCKETS + ("gpu_shared_mib", "gpu_shared_limit_mib",
                    "gpu_dedicated_mib", "vcpus")


def validate(data):
    if not isinstance(data, dict):
        raise ValueError("expected a JSON object")
    missing = [name for name in FIELDS if name not in data]
    if missing:
        raise ValueError("missing required fields: " + ", ".join(missing))
    for name in FIELDS:
        value = data[name]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(name + " must be numeric (not boolean)")
        try:
            finite = math.isfinite(value)
        except (OverflowError, ValueError):
            finite = False
        if not finite or value < 0:
            raise ValueError(name + " must be finite and nonnegative")
    if data["vcpus"] != 1 or not isinstance(data["vcpus"], int):
        raise ValueError("exactly one integer guest vCPU required")
    if data["gpu_dedicated_mib"] != 0:
        raise ValueError("shared-GPU profile prohibits dedicated VRAM")
    if data["gpu_shared_limit_mib"] > 512:
        raise ValueError("shared GPU limit exceeds 512 MiB")
    if data["gpu_shared_mib"] > data["gpu_shared_limit_mib"]:
        raise ValueError("shared GPU resident allocation exceeds its limit")
    if data["gpu_shared_mib"] > data["graphics_mib"]:
        raise ValueError("shared GPU resident bytes must be part of graphics_mib")
    # Check each *nonnegative* bucket before summing, avoiding floating overflow
    # and preserving the strict 512 MiB physical cap even for malicious inputs.
    for name in BUCKETS:
        if data[name] > 512:
            raise ValueError(name + " exceeds 512 MiB")
    total = math.fsum(data[name] for name in BUCKETS)
    if total > 512:
        raise ValueError("resident guest total exceeds 512 MiB")
    return {"pass": True, "guest_resident_mib": total,
            "guest_headroom_mib": 512 - total,
            "gpu_shared_resident_mib": data["gpu_shared_mib"],
            "note": "self-reported values; not measured performance or game compatibility"}


def main():
    if len(sys.argv) != 2:
        print("usage: validate.py evidence.json", file=sys.stderr)
        return 2
    try:
        with open(sys.argv[1], "r", encoding="utf-8") as handle:
            data = json.load(handle)
        print(json.dumps(validate(data), sort_keys=True))
        return 0
    except (OSError, ValueError, TypeError) as exc:
        print(json.dumps({"pass": False, "error": str(exc)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
