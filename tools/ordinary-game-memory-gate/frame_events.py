#!/usr/bin/env python3
"""Check consistency of bounded physical-frame ALLOC/FREE traces, not provenance."""
import argparse
import json
import sys
from check import CAP, COMPONENTS, EvidenceError, integer, load_bounded, require

PAGE_BYTES = 4096
MAX_EVENTS = 300_000
MAX_EVENT_BYTES = 32 * 1024 * 1024
KINDS = frozenset(COMPONENTS)


def initial_inventory(doc):
    """Return validated initial physical frame ownership for schema v2.

    This is a *declared* inventory and requires independent guest provenance.
    Schema v1 is unchanged and begins empty.
    """
    version = doc.get("schema_version")
    if type(version) is int and version == 1:
        require(set(doc) == {"schema_version", "source", "capture_started_empty",
                            "page_bytes", "guest_physical_capacity_bytes", "events"},
                "unexpected or missing v1 frame-event fields")
        require(doc["capture_started_empty"] is True, "v1 must start empty")
        return {}, 0
    require(type(version) is int and version == 2, "unsupported schema version")
    require(set(doc) == {"schema_version", "source", "capture_started_empty",
                        "capture_start_ns", "initial_frames", "page_bytes",
                        "guest_physical_capacity_bytes", "events"},
            "unexpected or missing v2 frame-event fields")
    require(doc["capture_started_empty"] is False,
            "v2 requires an explicit nonempty initial inventory")
    start = integer(doc["capture_start_ns"], "capture_start_ns")
    capacity = integer(doc["guest_physical_capacity_bytes"], "guest capacity", 1)
    frames = doc["initial_frames"]
    require(type(frames) is list and 1 <= len(frames) <= capacity // PAGE_BYTES,
            "v2 initial frames must be a bounded, nonempty list")
    live = {}
    for i, item in enumerate(frames):
        require(type(item) is dict and set(item) == {"frame_id", "kind"},
                f"initial frame {i}: invalid shape")
        frame = integer(item["frame_id"], f"initial frame {i} id")
        require(frame < capacity // PAGE_BYTES, f"initial frame {i}: outside guest RAM")
        kind = item["kind"]
        require(type(kind) is str and kind in KINDS,
                f"initial frame {i}: invalid ownership category")
        require(frame not in live, f"initial frame {i}: duplicate frame")
        live[frame] = kind
    return live, start


def replay(doc):
    require(type(doc) is dict, "frame-event report must be a JSON object")
    initial, start_ns = initial_inventory(doc)
    require(doc["source"] == "warden_guest_physical_frame_events",
            "requires physical frame events, not host or virtual page addresses")
    require(integer(doc["page_bytes"], "page_bytes", 1) == PAGE_BYTES, "requires 4096-byte physical frames")
    capacity = integer(doc["guest_physical_capacity_bytes"], "guest capacity", 1)
    require(capacity <= CAP and capacity % PAGE_BYTES == 0, "invalid configured guest capacity")
    events = doc["events"]
    require(type(events) is list and (len(events) >= 1 or initial) and len(events) <= MAX_EVENTS,
            "event count must be 1..300000")
    live = dict(initial)
    max_live = len(live)
    last_time = start_ns
    allocations = releases = 0
    for index, ev in enumerate(events):
        require(type(ev) is dict and set(ev) == {"time_ns", "op", "frame_id", "kind"},
                f"event {index}: invalid shape")
        t = integer(ev["time_ns"], f"event {index} time_ns")
        require(t >= last_time, f"event {index}: timestamp reversal")
        last_time = t
        frame = integer(ev["frame_id"], f"event {index} frame_id")
        require(frame < capacity // PAGE_BYTES, f"event {index}: frame outside configured physical RAM")
        kind = ev["kind"]
        require(type(kind) is str and kind in KINDS, f"event {index}: invalid accounting category")
        op = ev["op"]
        require(op in ("alloc", "free"), f"event {index}: invalid operation")
        if op == "alloc":
            require(frame not in live, f"event {index}: double allocation")
            live[frame] = kind
            allocations += 1
            max_live = max(max_live, len(live))
        else:
            require(frame in live, f"event {index}: free of unallocated frame")
            require(live[frame] == kind, f"event {index}: mismatched frame accounting category")
            del live[frame]
            releases += 1
    return {
        "trace_consistent": True,
        "peak_tracked_frame_bytes": max_live * PAGE_BYTES,
        "end_tracked_frame_bytes": len(live) * PAGE_BYTES,
        "alloc_events": allocations,
        "free_events": releases,
        "event_count": len(events),
        "initial_frame_count": len(initial),
        "capture_start_ns": start_ns,
        "note": "consistency of submitted trace only; no proof events came from Warden or include all guest RAM"
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("report")
    args = ap.parse_args(argv)
    try:
        out = replay(load_bounded(args.report, max_bytes=MAX_EVENT_BYTES))
    except (OSError, ValueError, TypeError, UnicodeError, RecursionError) as exc:
        print(json.dumps({"trace_consistent": False, "reason": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps(out, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
