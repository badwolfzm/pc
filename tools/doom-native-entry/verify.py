#!/usr/bin/env python3
"""Verify actual pinned Doom relocatable entry + reviewed SYS_EXIT closure."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PROTECTED = frozenset({"system","fopen","fclose","fread","fwrite","fseek",
                       "ftell","remove","rename"})
EXPORT = "_start"
CLOSED = "exit"

def symbols(obj):
    out = subprocess.run(["nm","-P","-g",str(obj)],check=True,
                         capture_output=True,text=True).stdout
    definitions, undefined = set(), set()
    for line in out.splitlines():
        fields = line.split()
        if len(fields) < 2:
            raise ValueError("invalid global nm entry")
        symbol, typ = fields[:2]
        if typ == "U": undefined.add(symbol)
        elif typ in ("T","D","B","R","C","A","W","V"):
            if symbol in definitions: raise ValueError("duplicate global definition")
            definitions.add(symbol)
        else: raise ValueError("unexpected nm kind: " + typ)
    return definitions, undefined

def verify(before, after, entry, exit_provider):
    original_defs, missing_before = symbols(before)
    combined_defs, missing_after = symbols(after)
    entry_defs, entry_imports = symbols(entry)
    exit_defs, exit_imports = symbols(exit_provider)
    if EXPORT in original_defs or CLOSED in original_defs:
        raise ValueError("existing entry or thread-exit definition")
    if entry_defs != {EXPORT} or entry_imports != {
        "doomgeneric_Create", "doomgeneric_Tick", "exit"}:
        raise ValueError("native entry exports or depends on unexpected symbol")
    if exit_defs != {CLOSED} or exit_imports:
        raise ValueError("thread exit provider must export exit alone, without imports")
    if EXPORT not in combined_defs or CLOSED not in combined_defs:
        raise ValueError("entry or exit absent from combined real Doom object")
    if CLOSED not in missing_before or missing_after != missing_before - {CLOSED}:
        raise ValueError("unexpected baseline import delta")
    if not PROTECTED.issubset(missing_after):
        raise ValueError("unreviewed host operations masked")
    return {
        "real_upstream_doom_partial_link": True,
        "defined_entry": EXPORT,
        "native_entry_provider_exports": sorted(entry_defs),
        "closed_exactly": [CLOSED],
        "before_unresolved": len(missing_before),
        "after_unresolved": len(missing_after),
        "remaining_imports": sorted(missing_after),
        "entry_object_sha256": hashlib.sha256(entry.read_bytes()).hexdigest(),
        "combined_et_rel_sha256": hashlib.sha256(after.read_bytes()).hexdigest(),
        "iwad_filename": "freedoom1.wad",
        "iwad_grant_installed": False,
        "entry_requested_frame_limit": 300,
        "actual_warden_game_boot": False,
        "measured_guest_512_mib": None,
        "measured_fps": None
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("before","after","entry","exit","report"):
        parser.add_argument(key,type=Path)
    a=parser.parse_args()
    value=verify(a.before,a.after,a.entry,a.exit)
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(value,indent=2,sort_keys=True)+"\n")
    print(json.dumps({k:value[k] for k in ("defined_entry","closed_exactly",
        "before_unresolved","after_unresolved")},sort_keys=True))

if __name__=="__main__":main()
