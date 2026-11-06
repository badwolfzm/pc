#!/usr/bin/env python3
"""Offline 512 MiB guest page-trace model, NOT a Warden benchmark."""
import argparse
from collections import OrderedDict, defaultdict
import json
from pathlib import Path

MIB = 1024 * 1024
PAGE = 4096
CAP = 512 * MIB
MAX_EVENTS = 200000
MAX_FILE = 8 * MIB

def number(x, label, minimum=0):
    if type(x) is not int or not minimum <= x <= 2**53:
        raise ValueError(label + " must be a bounded integer")
    return x

def simulate(doc):
    if type(doc) is not dict:
        raise ValueError("root must be an object")
    overhead = doc.get("non_game_resident_bytes")
    if type(overhead) is not dict or not overhead:
        raise ValueError("non_game_resident_bytes must be a nonempty breakdown")
    total = 0
    for key, size in overhead.items():
        if type(key) is not str or not key or len(key) > 80:
            raise ValueError("invalid overhead component name")
        total += number(size, key)
        if total >= CAP:
            raise ValueError("no game memory remains under 512 MiB cap")
    capacity = (CAP - total) // PAGE
    if capacity < 1:
        raise ValueError("less than one game page available")
    events = doc.get("events")
    if type(events) is not list or not 1 <= len(events) <= MAX_EVENTS:
        raise ValueError("events must contain 1..200000 entries")
    resident = OrderedDict()
    dirty = set()
    by_window = defaultdict(set)
    misses = writes = 0
    previous_ms = -1
    for event in events:
        if type(event) is not dict or set(event) != {"t_ms", "page", "write"}:
            raise ValueError("event requires exactly t_ms/page/write")
        t = number(event["t_ms"], "timestamp")
        p = number(event["page"], "page")
        if type(event["write"]) is not bool or t < previous_ms:
            raise ValueError("invalid write flag or nonmonotone timestamp")
        previous_ms = t
        by_window[t // 100].add(p)
        if p not in resident:
            misses += 1
            if len(resident) == capacity:
                evicted, _ = resident.popitem(last=False)
                if evicted in dirty:
                    dirty.remove(evicted)
                    writes += 1
            resident[p] = None
        else:
            resident.move_to_end(p)
        if event["write"]:
            dirty.add(p)
    hot = max(map(len, by_window.values()))
    report = {
        "scope": "offline synthetic/or externally measured trace model; not runtime enforcement",
        "guest_ram_cap_bytes": CAP,
        "non_game_bytes": total,
        "game_page_capacity": capacity,
        "game_resident_limit_bytes": capacity * PAGE,
        "events": len(events),
        "page_ins": misses,
        "dirty_eviction_page_outs": writes,
        "modeled_swap_read_bytes": misses * PAGE,
        "modeled_swap_write_bytes": writes * PAGE,
        "max_distinct_pages_per_100ms": hot,
        "max_distinct_bytes_per_100ms": hot * PAGE,
        "working_set_exceeds_capacity_in_100ms_window": hot > capacity,
        "notes": [
            "Cold misses may be asset reads, not necessarily swap reads.",
            "Distinct pages per window do not establish simultaneous residency or impossibility.",
            "No I/O scheduling, compression, prefetch, render timing, CPU cost, FPS or swap latency modeled.",
            "Assumes all events describe 4 KiB game pages and fully reserved non-game memory.",
            "Dirty pages still resident at trace end are not counted as writes."
        ]
    }
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        if args.trace.stat().st_size > MAX_FILE:
            raise ValueError("trace exceeds 8 MiB")
        doc = json.loads(args.trace.read_text(encoding="utf-8"),
                         parse_constant=lambda x: (_ for _ in ()).throw(ValueError("nonfinite JSON")))
        print(json.dumps(simulate(doc), sort_keys=True, indent=2))
        return 0
    except (OSError, ValueError, UnicodeError, RecursionError) as e:
        print(json.dumps({"status": "FAIL", "error": str(e)}))
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
