#!/usr/bin/env python3
"""Fail-closed validation of self-reported WHOLE-GUEST physical RAM samples."""
import argparse
import json
import sys
from pathlib import Path

CAP = 512 * 1024 * 1024
COMPONENTS = ("kernel", "runtime", "game", "framebuffer", "paging", "graphics", "other")
MAX_SAMPLES = 100_000
MAX_JSON_BYTES = 4 * 1024 * 1024


class EvidenceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def integer(value, name, min_value=0):
    require(type(value) is int and value >= min_value, f"{name} must be integer >= {min_value}")
    return value


def validate(report):
    require(type(report) is dict, "report must be an object")
    version = report.get("schema_version")
    require(type(version) is int and version in (1, 2),
            "snapshot schema must be 1 or 2")
    fields = {
        "schema_version", "source", "complete", "vcpu_count",
        "guest_physical_capacity_bytes", "host_peak_rss_bytes",
        "peak_total_bytes", "peak_method", "samples"
    }
    if version == 2:
        fields.add("boot_id")
        boot_id = report.get("boot_id")
        require(type(boot_id) is str and len(boot_id) == 64 and
                all(ch in "0123456789abcdef" for ch in boot_id),
                "snapshot boot_id must be 64 lowercase hex characters")
    require(set(report) == fields,
            "required report fields must be present with no unknown fields")
    require(report["source"] == "warden_guest_physical",
            "source must identify guest physical memory, not process RSS or virtual space")
    require(report["complete"] is True, "report must explicitly claim complete accounting")
    require(integer(report["vcpu_count"], "vcpu_count", 1) == 1, "requires one guest vCPU")
    capacity = integer(report["guest_physical_capacity_bytes"], "guest physical capacity", 1)
    require(capacity <= CAP, "configured guest physical capacity exceeds 512 MiB")
    integer(report["host_peak_rss_bytes"], "host peak RSS", 1)
    peak = integer(report["peak_total_bytes"], "peak total guest memory")
    require(report["peak_method"] == "physical_frame_event_high_water",
            "peak must come from continuous physical-frame allocation/free event accounting")
    require(peak <= capacity, "event high-water exceeds guest physical capacity")
    samples = report["samples"]
    require(type(samples) is list and 2 <= len(samples) <= MAX_SAMPLES,
            "two or more bounded physical memory samples required")
    last_time = -1
    observed_peak = 0
    for idx, sample in enumerate(samples):
        require(type(sample) is dict and set(sample) == {"time_ms", "components", "total_bytes"},
                f"samples[{idx}] must have exact time/components/total fields")
        t = integer(sample["time_ms"], f"samples[{idx}].time_ms")
        require(t > last_time, "sample times must be strictly increasing")
        last_time = t
        parts = sample["components"]
        require(type(parts) is dict and set(parts) == set(COMPONENTS),
                f"samples[{idx}] must account for all components")
        used = sum(integer(parts[k], f"samples[{idx}].{k}") for k in COMPONENTS)
        total = integer(sample["total_bytes"], f"samples[{idx}].total_bytes")
        require(used == total, "reported sample total != component sum")
        require(total <= capacity, "sampled physical memory exceeds configured capacity")
        observed_peak = max(observed_peak, total)
    require(observed_peak <= peak, "event high-water must be at least the largest observed sample")
    return {"accepted": True, "configured_mib": round(capacity / 1048576, 3),
            "event_peak_mib": round(peak / 1048576, 3),
            "sampled_peak_mib": round(observed_peak / 1048576, 3),
            "margin_mib": round((capacity - peak) / 1048576, 3),
            "samples": len(samples), "note": "self-reported evidence only; not independently measured or enforced"}


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise EvidenceError(f"duplicate JSON key: {key}")
        out[key] = value
    return out


def load_bounded(path, max_bytes=MAX_JSON_BYTES):
    require(type(max_bytes) is int and MAX_JSON_BYTES <= max_bytes <= 32 * 1024 * 1024,
            "invalid JSON byte limit")
    source = Path(path)
    if source.stat().st_size > max_bytes:
        raise EvidenceError("report exceeds permitted input limit")
    with source.open("rb") as stream:
        payload = stream.read(max_bytes + 1)
    if len(payload) > max_bytes:
        raise EvidenceError("report exceeds 4 MiB input limit")
    return json.loads(payload, object_pairs_hook=unique_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(EvidenceError(value)))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", help="JSON evidence file")
    args = parser.parse_args(argv)
    try:
        report = load_bounded(args.report)
        result = validate(report)
    except (OSError, ValueError, TypeError, UnicodeError) as exc:
        print(json.dumps({"accepted": False, "reason": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
