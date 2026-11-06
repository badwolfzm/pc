#!/usr/bin/env python3
"""Validate *reported*, disjoint physical RAM telemetry for ordinary PC games."""
import argparse
import json
import pathlib
import sys

MIB = 1024 * 1024
LIMIT = 512 * MIB
MAX_JSON = 1024 * 1024
MAX_SAMPLES = 10000
CATEGORIES = ("kernel", "runtime", "game", "graphics", "page_cache", "swap_metadata", "other")

class Invalid(ValueError):
    pass

def integer(value, label, minimum=0, maximum=(1 << 63)-1):
    if type(value) is not int or not minimum <= value <= maximum:
        raise Invalid(f"{label} must be integer in [{minimum},{maximum}]")
    return value

def fields(obj, label, needed, optional=()):
    if not isinstance(obj, dict) or set(needed) - set(obj) or set(obj) - set(needed) - set(optional):
        raise Invalid(f"{label}: required keys {sorted(needed)}; allowed extras {sorted(optional)}")
    return obj

def validate(doc):
    fields(doc, "root", ("schema", "platform", "configuration", "samples", "provenance"))
    if doc["schema"] != "ordinary512-v1" or doc["platform"] != "warden-qemu":
        raise Invalid("unsupported schema/platform")
    cfg = fields(doc["configuration"], "configuration",
                 ("vcpus", "guest_ram_bytes", "graphics_backend", "authorized_swap_bytes"))
    if integer(cfg["vcpus"], "vcpus", 1, 1) != 1:
        raise Invalid("one vCPU required")
    cap = integer(cfg["guest_ram_bytes"], "guest_ram_bytes", 1, LIMIT)
    if cap != LIMIT:
        raise Invalid("512 MiB experiment requires exact configured 512 MiB; use another profile separately")
    if cfg["graphics_backend"] not in ("cpu-software", "integrated-gpu"):
        raise Invalid("graphics backend must be declared")
    integer(cfg["authorized_swap_bytes"], "authorized_swap_bytes", 0)
    provenance = fields(doc["provenance"], "provenance",
                        ("commit", "game_sha256", "source", "telemetry_kind"))
    if not isinstance(provenance["commit"], str) or len(provenance["commit"]) != 40 or any(c not in "0123456789abcdef" for c in provenance["commit"]):
        raise Invalid("commit must be exact lowercase SHA-1")
    if not isinstance(provenance["game_sha256"], str) or len(provenance["game_sha256"]) != 64 or any(c not in "0123456789abcdef" for c in provenance["game_sha256"]):
        raise Invalid("game_sha256 must be exact lowercase SHA-256")
    if not isinstance(provenance["source"], str) or not provenance["source"].strip() or len(provenance["source"]) > 500:
        raise Invalid("source must identify actual telemetry provenance")
    if provenance["telemetry_kind"] not in ("synthetic-fixture", "guest-instrumented"):
        raise Invalid("explicit telemetry kind required")
    samples = doc["samples"]
    if not isinstance(samples, list) or not 1 <= len(samples) <= MAX_SAMPLES:
        raise Invalid("samples must contain 1..10000 entries")
    peak = 0
    prev_time = -1
    peak_at = 0
    for idx, sample in enumerate(samples):
        fields(sample, f"sample[{idx}]", ("t_ms", "resident", "swap_read_bytes", "swap_written_bytes", "page_faults"))
        t = integer(sample["t_ms"], f"sample[{idx}].t_ms")
        if t <= prev_time:
            raise Invalid("sample times must strictly increase")
        prev_time = t
        resident = fields(sample["resident"], f"sample[{idx}].resident", CATEGORIES)
        total = 0
        for name in CATEGORIES:
            total += integer(resident[name], f"sample[{idx}].resident.{name}")
        for name in ("swap_read_bytes", "swap_written_bytes", "page_faults"):
            integer(sample[name], f"sample[{idx}].{name}")
        if idx:
            for name in ("swap_read_bytes", "swap_written_bytes", "page_faults"):
                if sample[name] < samples[idx - 1][name]:
                    raise Invalid(f"{name} counter decreased")
        if total > cap:
            raise Invalid(f"sample[{idx}] physical resident {total} exceeds configured {cap}")
        if total > peak:
            peak, peak_at = total, t
    return {
        "valid": True, "schema": "ordinary512-v1", "profile": cfg["graphics_backend"],
        "telemetry_kind": provenance["telemetry_kind"], "cap_bytes": cap,
        "peak_reported_resident_bytes": peak, "peak_at_ms": peak_at,
        "headroom_at_peak_bytes": cap - peak, "samples_checked": len(samples),
        "caveat": "Validates supplied disjoint accounting only, not independent Warden/QEMU measurement, game execution, page-fault latency, FPS, or honest provenance."
    }

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        with args.report.open("rb") as f:
            raw = f.read(MAX_JSON + 1)
        if len(raw) > MAX_JSON:
            raise Invalid("input exceeds 1 MiB")
        result = validate(json.loads(raw))
    except (OSError, UnicodeError, json.JSONDecodeError, Invalid, RecursionError) as exc:
        print(json.dumps({"valid": False, "reason": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0

if __name__ == "__main__":
    sys.exit(main())
