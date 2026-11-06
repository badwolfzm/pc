#!/usr/bin/env python3
"""Source-derived Doom startup zone capacity check, not a guest memory measurement."""
import argparse
import json
from pathlib import Path
import re

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
MIB = 1024 * 1024

class GateFailure(ValueError):
    pass

def extract_source_zone(text):
    values = {}
    for key in ("DEFAULT_RAM", "MIN_RAM"):
        matches = re.findall(r"^\s*#\s*define\s+" + key + r"\s+([0-9]+)\b", text, re.M)
        if len(matches) != 1:
            raise GateFailure("expected exactly one numeric " + key)
        values[key] = int(matches[0])
    # Require direct evidence that this allocation is the one used by I_ZoneBase.
    if not re.search(r"\bI_ZoneBase\s*\([^)]*\)\s*\{", text):
        raise GateFailure("I_ZoneBase not found")
    if "AutoAllocMemory(size, default_ram, min_ram)" not in text:
        raise GateFailure("unknown zone allocation call")
    if not re.search(r"\*size\s*=\s*default_ram\s*\*\s*1024\s*\*\s*1024\s*;", text):
        raise GateFailure("unrecognized zone byte sizing")
    if not re.search(r"zonemem\s*=\s*malloc\s*\(\s*\*size\s*\)\s*;", text):
        raise GateFailure("unrecognized zone malloc")
    if not re.search(r"default_ram\s*=\s*DEFAULT_RAM\s*;", text) or not re.search(r"min_ram\s*=\s*MIN_RAM\s*;", text):
        raise GateFailure("no verified default zone selection")
    if not 1 <= values["MIN_RAM"] <= values["DEFAULT_RAM"] <= 4096:
        raise GateFailure("unreasonable or inverted zone limits")
    return values

def verify(source_text, arena_bytes):
    if type(arena_bytes) is not int or arena_bytes <= 0:
        raise GateFailure("positive integer arena bytes required")
    data = extract_source_zone(source_text)
    default_bytes = data["DEFAULT_RAM"] * MIB
    minimum_bytes = data["MIN_RAM"] * MIB
    result = {
        "upstream_pin": PIN,
        "default_zone_mib": data["DEFAULT_RAM"],
        "minimum_zone_mib": data["MIN_RAM"],
        "default_zone_bytes": default_bytes,
        "minimum_zone_bytes": minimum_bytes,
        "candidate_arena_bytes": arena_bytes,
        "capacity_pass_for_default_zone": arena_bytes > default_bytes,
        "candidate_doom_boot_verified": False,
        "note": "Even capacity > zone is not proof: headers, other malloc users and runtime demand consume space.",
    }
    if arena_bytes <= default_bytes:
        raise GateFailure("candidate arena " + str(arena_bytes) +
                          " bytes cannot hold default Doom zone " +
                          str(default_bytes) + " bytes plus allocator overhead")
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True, help="pinned upstream i_system.c")
    p.add_argument("--arena-bytes", type=int, required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--report", type=Path)
    a = p.parse_args()
    if a.commit != PIN:
        p.error("upstream revision must equal pinned hash")
    try:
        if a.source.stat().st_size > 500000:
            raise GateFailure("unbounded source file")
        result = verify(a.source.read_text(encoding="utf-8"), a.arena_bytes)
    except (GateFailure, OSError, UnicodeError) as e:
        p.error(str(e))
    if a.report:
        a.report.parent.mkdir(parents=True, exist_ok=True)
        a.report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, sort_keys=True))

if __name__ == "__main__":
    main()
