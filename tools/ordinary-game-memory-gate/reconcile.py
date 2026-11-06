#!/usr/bin/env python3
"""Cross-check two *self-reported* whole-guest physical-memory evidence streams.

The frame event sequence must cover all physical frames from an empty start.
Every sampled category must equal the event-reconstructed category at that time.
This is consistency checking, not proof that an instrumented Warden guest supplied it.
"""
import argparse
import json
import sys

from check import COMPONENTS, EvidenceError, load_bounded, require, validate
from frame_events import MAX_EVENT_BYTES, PAGE_BYTES, replay, initial_inventory

NANOSECONDS_PER_MILLISECOND = 1_000_000


def reconcile(snapshot, trace):
    snap = validate(snapshot)
    events = replay(trace)
    if trace["schema_version"] == 3:
        require(snapshot["schema_version"] == 2,
                "v3 frame trace requires boot-bound snapshot v2")
        require(snapshot["boot_id"] == trace["boot_id"],
                "snapshot and frame trace belong to different boot IDs")
    else:
        require(snapshot["schema_version"] == 1,
                "legacy frame traces require legacy snapshot schema")
    require(snapshot["guest_physical_capacity_bytes"] ==
            trace["guest_physical_capacity_bytes"], "trace and snapshot capacities disagree")
    require(snapshot["peak_total_bytes"] ==
            events["peak_tracked_frame_bytes"], "declared high-water disagrees with event replay")
    require(snapshot["peak_method"] == "physical_frame_event_high_water",
            "missing physical frame-event high-water claim")

    # Sample at t means the state after ALL events with time_ns <= t*1e6.
    live, capture_start_ns = initial_inventory(trace)
    counts = {name: 0 for name in COMPONENTS}
    for kind in live.values():
        counts[kind] += 1
    cursor = 0
    event_list = trace["events"]
    for idx, sample in enumerate(snapshot["samples"]):
        boundary = sample["time_ms"] * NANOSECONDS_PER_MILLISECOND
        require(boundary >= capture_start_ns,
                f"sample {idx}: timestamp precedes trace capture start")
        while cursor < len(event_list) and event_list[cursor]["time_ns"] <= boundary:
            ev = event_list[cursor]
            frame, kind = ev["frame_id"], ev["kind"]
            if ev["op"] == "alloc":
                require(frame not in live, "duplicate frame during reconciliation")
                live[frame] = kind
                counts[kind] += 1
            else:
                require(live.get(frame) == kind, "invalid release during reconciliation")
                del live[frame]
                counts[kind] -= 1
            cursor += 1
        for kind in COMPONENTS:
            expected = counts[kind] * PAGE_BYTES
            require(sample["components"][kind] == expected,
                    f"sample {idx}: {kind} differs from physical frame-event count")
        require(sample["total_bytes"] == len(live) * PAGE_BYTES,
                f"sample {idx}: physical-memory total does not match events")

    # Prevent a report ending before the event stream does: all events must be
    # covered by the final sample, otherwise the producer's interval is ambiguous.
    require(cursor == len(event_list),
            "frame event occurs after the last memory sample")
    return {
        "reports_consistent": True,
        "samples_matched": len(snapshot["samples"]),
        "events_replayed": events["event_count"],
        "peak_tracked_frame_bytes": events["peak_tracked_frame_bytes"],
        "note": "self-reported cross-report consistency only; not independently verified Warden memory"
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshots", help="whole-guest memory sample report JSON")
    parser.add_argument("frame_events", help="physical-frame alloc/free event JSON")
    args = parser.parse_args(argv)
    try:
        snap = load_bounded(args.snapshots)
        trace = load_bounded(args.frame_events, max_bytes=MAX_EVENT_BYTES)
        result = reconcile(snap, trace)
    except (OSError, ValueError, TypeError, UnicodeError, RecursionError) as exc:
        print(json.dumps({"reports_consistent": False, "reason": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
