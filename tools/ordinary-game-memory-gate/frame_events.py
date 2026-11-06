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


def replay(doc):
    require(type(doc) is dict and set(doc) == {
        "schema_version", "source", "capture_started_empty",
        "page_bytes", "guest_physical_capacity_bytes", "events"
    }, "unexpected or missing frame-event fields")
    require(type(doc["schema_version"]) is int and doc["schema_version"] == 1, "bad schema")
    require(doc["source"] == "warden_guest_physical_frame_events",
            "requires physical frame events, not host or virtual page addresses")
    require(doc["capture_started_empty"] is True, "must begin before first frame allocation")
    require(integer(doc["page_bytes"], "page_bytes", 1) == PAGE_BYTES, "requires 4096-byte physical frames")
    capacity = integer(doc["guest_physical_capacity_bytes"], "guest capacity", 1)
    require(capacity <= CAP and capacity % PAGE_BYTES == 0, "invalid configured guest capacity")
    events = doc["events"]
    require(type(events) is list and 1 <= len(events) <= MAX_EVENTS,
            "event count must be 1..300000")
    live = {}
    max_live = 0
    last_time = -1
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
