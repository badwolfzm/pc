#!/usr/bin/env python3
"""Fail-closed process lifecycle inventory validator (report only, not attestation)."""
import json
import sys
from pathlib import Path

LIMIT = 1024 * 1024
MAX_EVENTS = 10000

class Invalid(ValueError):
    pass

def require(value, message):
    if not value:
        raise Invalid(message)

def validate(doc):
    require(type(doc) is dict, "root must be an object")
    require(set(doc) == {"schema", "source", "boot_id", "complete", "vcpu_count", "events"}, "unexpected or missing root fields")
    require(doc["schema"] == 1, "unsupported schema")
    require(doc["source"] in ("synthetic", "guest-reported"), "invalid source")
    require(type(doc["boot_id"]) is str and len(doc["boot_id"]) == 64 and all(c in "0123456789abcdef" for c in doc["boot_id"]), "invalid boot ID")
    require(doc["complete"] is True, "incomplete inventory")
    require(type(doc["vcpu_count"]) is int and doc["vcpu_count"] == 1, "must report exactly one vCPU")
    events = doc["events"]
    require(type(events) is list and 3 <= len(events) <= MAX_EVENTS, "invalid event count")
    alive = {}
    seen = set()
    game_pids = set()
    peak = 0
    phase = "before"
    for i, ev in enumerate(events):
        require(type(ev) is dict and set(ev) == ({"seq", "event"} if ev.get("event") in ("boot", "end") else {"seq", "event", "pid", "role"}), f"event {i}: malformed fields")
        require(type(ev["seq"]) is int and ev["seq"] == i, f"event {i}: sequence gap")
        kind = ev["event"]
        require(kind in ("boot", "spawn", "exit", "end"), f"event {i}: unknown event")
        if kind == "boot":
            require(phase == "before", "boot must occur once at start")
            phase = "running"
        elif kind == "end":
            require(phase == "running" and i == len(events) - 1, "end must be last")
            require(not alive, "live process at end")
            phase = "ended"
        else:
            require(phase == "running", "process event outside boot")
            pid = ev["pid"]
            role = ev["role"]
            require(type(pid) is int and 1 <= pid <= 2147483647, "invalid pid")
            require(role in ("game", "helper"), "invalid role")
            if kind == "spawn":
                require(pid not in seen and pid not in alive, "reused or duplicate pid")
                seen.add(pid)
                alive[pid] = role
                if role == "game":
                    game_pids.add(pid)
                peak = max(peak, len(alive))
                require(len(alive) <= 1, "more than one guest userspace process concurrently alive")
            else:
                require(alive.get(pid) == role, "exit has missing pid or wrong role")
                del alive[pid]
    require(phase == "ended", "missing end marker")
    require(len(game_pids) == 1, "expected exactly one game process for entire boot")
    return {"status": "pass", "source": doc["source"], "peak_guest_userspace_processes": peak,
            "game_pid": next(iter(game_pids)), "events_checked": len(events),
            "limitations": "self-reported inventory; not independently authenticated guest telemetry or FPS/RAM measurement"}

def main(argv):
    if len(argv) != 2:
        print("usage: check.py REPORT.json", file=sys.stderr)
        return 2
    try:
        path = Path(argv[1])
        with path.open("rb") as f:
            raw = f.read(LIMIT + 1)
        require(len(raw) <= LIMIT, "report exceeds 1 MiB")
        doc = json.loads(raw)
        result = validate(doc)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (Invalid, OSError, ValueError, UnicodeError) as e:
        print(json.dumps({"status": "fail", "reason": str(e)}), file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main(sys.argv))
