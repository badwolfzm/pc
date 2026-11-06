#!/usr/bin/env python3
"""Fail-closed controlled-direction Doom host test evidence gate.

Only host-side mocked Warden syscalls; does not authenticate its log producer.
"""
import argparse
import json
from pathlib import Path
import re
import sys

PREFIX = "DOOM_EVIDENCE_V1 "
FIELDS = ("mode", "frames", "level_frames", "player_frames",
          "start_x", "start_y", "end_x", "end_y", "pressed", "released",
          "consumed_down", "consumed_up", "first_hash", "last_hash")
NUMERIC = set(FIELDS) - {"mode", "first_hash", "last_hash"}
HEX = re.compile(r"^[0-9a-f]{16}$")
DEC = re.compile(r"^-?(?:0|[1-9][0-9]*)$")
MAX_LOG = 1024 * 1024

class Rejected(ValueError):
    pass

def extract(log, expected_mode):
    if len(log) > MAX_LOG:
        raise Rejected("oversized log")
    if re.search(r"(?m)^FAIL\b", log) or "WARDEN ADAPTER EXIT" in log:
        raise Rejected("host adapter or game reported failure")
    lines = [line for line in log.splitlines() if line.startswith(PREFIX)]
    if len(lines) != 1:
        raise Rejected("expected exactly one structured evidence line")
    values = {}
    parts = lines[0][len(PREFIX):].split(" ")
    if len(parts) != len(FIELDS):
        raise Rejected("missing or additional structured fields")
    for part in parts:
        if part.count("=") != 1:
            raise Rejected("invalid structured field encoding")
        key, value = part.split("=")
        if key in values or key not in FIELDS:
            raise Rejected("duplicate/unknown evidence key")
        if key in NUMERIC:
            if not DEC.fullmatch(value):
                raise Rejected("invalid integer for " + key)
            values[key] = int(value)
        else:
            values[key] = value
    if set(values) != set(FIELDS) or values["mode"] != expected_mode:
        raise Rejected("wrong test mode or evidence schema")
    for key in ("first_hash", "last_hash"):
        if not HEX.fullmatch(values[key]):
            raise Rejected("malformed framebuffer digest")
    if any(values[key] != 10 for key in ("frames", "level_frames", "player_frames")):
        raise Rejected("not exactly ten actual in-level player frames")
    if any(values[key] < 0 for key in ("pressed","released","consumed_down","consumed_up")):
        raise Rejected("negative input event count")
    return values

def compare(control_log, input_log, reverse_log=None):
    a, b = extract(control_log, "control"), extract(input_log, "intervention")
    for key in ("start_x", "start_y", "first_hash"):
        if a[key] != b[key]:
            raise Rejected("different starting conditions: " + key)
    if (a["start_x"],a["start_y"]) != (a["end_x"],a["end_y"]):
        raise Rejected("player moved in no-input control")
    for key in ("pressed","released","consumed_down","consumed_up"):
        if a[key] != 0 or b[key] != 1:
            raise Rejected("missing or unexpected " + key)
    if (b["start_x"],b["start_y"]) == (b["end_x"],b["end_y"]):
        raise Rejected("input did not produce player displacement")
    if b["first_hash"] == b["last_hash"]:
        raise Rejected("intervention did not alter framebuffer")
    reverse = extract(reverse_log, "reverse") if reverse_log is not None else None
    if reverse is not None:
        for key in ("start_x", "start_y", "first_hash"):
            if a[key] != reverse[key]:
                raise Rejected("reverse differs from control starting conditions: " + key)
        for key in ("pressed", "released", "consumed_down", "consumed_up"):
            if reverse[key] != 1:
                raise Rejected("reverse input not consumed: " + key)
        forward_dx = b["end_x"] - b["start_x"]
        reverse_dx = reverse["end_x"] - reverse["start_x"]
        if forward_dx <= 0 or reverse_dx >= 0:
            raise Rejected("forward/reverse movement lacks opposite X directions")
        if reverse["first_hash"] == reverse["last_hash"]:
            raise Rejected("reverse input did not alter framebuffer")
    result = {
        "schema": "doom-host-controlled-directions-v2" if reverse else "doom-host-controlled-input-v1",
        "scope": "actual upstream Doom/Freedoom running on Linux host with Warden callback mock; NOT Warden guest or FPS",
        "control": a, "intervention": b,
        "player_displacement_fixed_units": {
            "dx": b["end_x"] - b["start_x"], "dy": b["end_y"] - b["start_y"]
        },
        "control_stationary": True,
        "intervention_moved": True,
        "both_runs_in_level": True,
        "input_consumed_by_upstream": True
    }
    if reverse:
        result["reverse"] = reverse
        result["reverse_displacement_fixed_units"] = {
            "dx": reverse["end_x"] - reverse["start_x"],
            "dy": reverse["end_y"] - reverse["start_y"],
        }
        result["opposite_direction_control_verified"] = True
    return result

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("control", type=Path)
    p.add_argument("intervention", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("reverse", type=Path, nargs="?")
    a = p.parse_args(argv)
    if len({x.resolve() for x in (a.control, a.intervention, a.reverse) if x is not None}) != (3 if a.reverse else 2):
        print("DOOM EVIDENCE REJECTED: same file twice", file=sys.stderr)
        return 1
    try:
        if any(x.stat().st_size > MAX_LOG for x in (a.control, a.intervention, a.reverse) if x is not None):
            raise Rejected("input log too large")
        result = compare(a.control.read_text(), a.intervention.read_text(),
                         a.reverse.read_text() if a.reverse else None)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print("PASS verified controlled native-engine host input/movement evidence; NOT Warden guest")
        return 0
    except (OSError, UnicodeError, ValueError) as err:
        print("DOOM EVIDENCE REJECTED:", err, file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
