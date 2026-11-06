#!/usr/bin/env python3
"""Fail-closed verification of actual engine frame callbacks, not fake FPS."""
import argparse
import json
import re
from pathlib import Path

FRAME = re.compile(r"FRAME,([0-9]+),([0-9]+),([0-9a-f]{16})$")
COUNT = re.compile(r"FRAME_COUNT,([0-9]+)$")


def percentile(values, n):
    ordered = sorted(values)
    return ordered[max(0, (len(ordered) * n + 99) // 100 - 1)]


def verify(lines, required=300):
    if not isinstance(required, int) or required < 3 or required > 10000:
        raise ValueError("invalid required frame count")
    frames = []
    declared = []
    for line in lines:
        line = line.rstrip("\n")
        if line.startswith("FRAME,"):
            m = FRAME.fullmatch(line)
            if not m:
                raise ValueError("malformed frame evidence")
            idx, stamp, digest = m.groups()
            frames.append((int(idx), int(stamp), digest))
            if len(frames) > required:
                raise ValueError("more frame records than expected")
        elif line.startswith("FRAME_COUNT,"):
            m = COUNT.fullmatch(line)
            if not m:
                raise ValueError("malformed frame count")
            declared.append(int(m.group(1)))
    if len(declared) != 1 or declared[0] != required or len(frames) != required:
        raise ValueError("incomplete or ambiguous real frame evidence")
    if [entry[0] for entry in frames] != list(range(required)):
        raise ValueError("frame index sequence invalid")
    if len(set(f[2] for f in frames)) < 2:
        raise ValueError("all framebuffer digests identical; no visual change evidenced")
    stamps = [f[1] for f in frames]
    if any(b <= a for a, b in zip(stamps, stamps[1:])):
        raise ValueError("frame timestamps not strictly increasing")
    elapsed = stamps[-1] - stamps[0]
    if elapsed <= 0:
        raise ValueError("nonpositive frame duration")
    gaps = [b - a for a, b in zip(stamps, stamps[1:])]
    return {
        "evidence_type": "actual upstream Doom callbacks in Linux host, not Warden guest",
        "frames": required,
        "distinct_frame_hashes": len(set(f[2] for f in frames)),
        "first_timestamp_ms": stamps[0],
        "last_timestamp_ms": stamps[-1],
        "elapsed_between_frames_ms": elapsed,
        "observed_paced_callback_hz": round((required - 1) * 1000 / elapsed, 3),
        "interval_ms_p50": percentile(gaps, 50),
        "interval_ms_p95": percentile(gaps, 95),
        "interval_ms_p99": percentile(gaps, 99),
        "limitations": [
            "35Hz-ish deliberate paced host test; not uncapped engine throughput or FPS on user's PC",
            "One Linux host CPU affinity; not a Warden -smp 1 guest",
            "Screen hashes show changed pixel buffers, not validated gameplay semantics",
            "No Warden RAM cap/peak memory, input, storage capability or Albion compatibility evidenced"
        ],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("log", type=Path)
    p.add_argument("--required", type=int, default=300)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    try:
        with a.log.open(encoding="utf-8") as stream:
            report = verify(stream, a.required)
        data = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if a.output:
            a.output.parent.mkdir(parents=True, exist_ok=True)
            a.output.write_text(data, encoding="utf-8")
        print(data, end="")
    except (ValueError, OSError) as e:
        p.error(str(e))


if __name__ == "__main__":
    main()
