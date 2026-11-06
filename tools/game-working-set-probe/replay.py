#!/usr/bin/env python3
"""Host-only 4KiB page-trace LRU model; NOT real paging, FPS or guest memory proof."""
import argparse
from collections import OrderedDict
import json
from pathlib import Path
import sys

PAGE = 4096
MIB = 1024 * 1024
MAX_FILE = 4 * MIB
MAX_EVENTS = 100000
WINDOWS = (16, 100, 1000)
MAX_PAGE = 1 << 48

class InvalidTrace(ValueError):
    pass

def integer(obj, key, minimum=0):
    v = obj.get(key)
    if type(v) is not int or v < minimum:
        raise InvalidTrace(f"{key} must be integer >= {minimum}")
    return v

def analyze(trace):
    if not isinstance(trace, dict):
        raise InvalidTrace("root must be object")
    cap = integer(trace, "guest_ram_bytes", PAGE)
    if cap != 512 * MIB:
        raise InvalidTrace("this acceptance profile requires exactly 512 MiB total guest RAM")
    reserves = trace.get("reserved_bytes")
    required = ("kernel", "runtime", "framebuffers", "cache", "metadata")
    if not isinstance(reserves, dict) or set(reserves) != set(required):
        raise InvalidTrace("reserved_bytes must include exactly kernel/runtime/framebuffers/cache/metadata")
    taken = sum(integer(reserves, k) for k in required)
    if taken >= cap:
        raise InvalidTrace("reserves exhaust guest RAM")
    if taken % PAGE:
        raise InvalidTrace("reserves must be 4KiB-page-aligned")
    capacity = (cap - taken) // PAGE
    events = trace.get("events")
    if not isinstance(events, list) or len(events) > MAX_EVENTS:
        raise InvalidTrace("events must be a list with <= 100000 items")
    resident = OrderedDict()
    dirty = set()
    faults = evictions = dirty_evictions = reads = writes = 0
    last_ms = -1
    page_windows = {w: {} for w in WINDOWS}
    for event in events:
        if not isinstance(event, dict) or set(event) != {"ms", "page", "write"}:
            raise InvalidTrace("event must contain exactly ms, page, write")
        ms = integer(event, "ms")
        page = integer(event, "page")
        if page >= MAX_PAGE or ms < last_ms or type(event["write"]) is not bool:
            raise InvalidTrace("page, timestamp order or write flag invalid")
        last_ms = ms
        for w in WINDOWS:
            bucket = ms // w
            page_windows[w].setdefault(bucket, set()).add(page)
        if event["write"]:
            writes += 1
        else:
            reads += 1
        if page in resident:
            resident.move_to_end(page)
        else:
            faults += 1
            if len(resident) >= capacity:
                old, _ = resident.popitem(last=False)
                evictions += 1
                if old in dirty:
                    dirty.remove(old)
                    dirty_evictions += 1
            resident[page] = None
        if event["write"]:
            dirty.add(page)
    windows = {}
    for w, buckets in page_windows.items():
        nums = [len(v) for _, v in sorted(buckets.items())]
        windows[str(w)] = {
            "observed_bins": len(nums),
            "max_unique_pages": max(nums, default=0),
            "max_unique_bytes": max(nums, default=0) * PAGE,
            "bins_exceed_resident_capacity": sum(n > capacity for n in nums),
        }
    return {
        "profile": "one-vcpu-512mib-guest-trace-model",
        "guest_ram_bytes": cap, "reserved_bytes": reserves,
        "resident_budget_bytes": capacity * PAGE, "resident_page_capacity": capacity,
        "events": len(events), "reads": reads, "writes": writes, "faults": faults,
        "evictions": evictions, "dirty_evictions": dirty_evictions,
        "model_page_in_bytes": faults * PAGE,
        "model_dirty_writeback_bytes": dirty_evictions * PAGE,
        "final_resident_pages": len(resident), "final_dirty_pages": len(dirty),
        "window_unique_demand": windows,
        "caveat": "Offline LRU trace model, not native Warden paging or measured FPS. Unique pages within a time bin need not be simultaneously resident; exceeding capacity indicates potential churn, not mathematical impossibility. Model excludes allocator/page-table overhead except explicit reserves."
    }

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("trace", type=Path)
    a = p.parse_args()
    try:
        if a.trace.stat().st_size > MAX_FILE:
            raise InvalidTrace("trace file too large")
        with a.trace.open("r", encoding="utf-8") as f:
            parsed = json.load(f)
        report = analyze(parsed)
        print(json.dumps({"ok": True, **report}, sort_keys=True, indent=2))
    except (OSError, UnicodeError, json.JSONDecodeError, InvalidTrace) as e:
        print(json.dumps({"ok": False, "error": str(e)}), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
