#!/usr/bin/env python3
"""Reject duplicate strong global symbol definitions across selected runtime objects."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

SYMBOL = re.compile(r"^[A-Za-z_.$][A-Za-z0-9_.$@]*$")
STRONG = frozenset(("T", "D", "B", "R", "A", "G", "S"))
MAX_OBJECTS = 128
MAX_BYTES = 64 * 1024 * 1024

def definitions(stdout, path):
    found = {}
    for line in stdout.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        sym, typ = fields[:2]
        if not SYMBOL.fullmatch(sym):
            continue
        if typ in STRONG:
            found[sym] = str(path)
    return found

def analyze(objects):
    if len(objects) < 2 or len(objects) > MAX_OBJECTS:
        raise ValueError("expected 2..128 explicit object files")
    if len({str(p.resolve()) for p in objects}) != len(objects):
        raise ValueError("duplicate file arguments")
    defs = {}
    for path in objects:
        if not path.is_file() or path.stat().st_size > MAX_BYTES:
            raise ValueError("missing/oversized object: "+str(path))
        run = subprocess.run(["nm","-P","-g","--defined-only",str(path)],
                             capture_output=True,text=True,timeout=15)
        if run.returncode:
            raise ValueError("nm failed: "+run.stderr[:240])
        for sym, owner in definitions(run.stdout,path).items():
            defs.setdefault(sym,[]).append(owner)
    dup={name:owners for name,owners in sorted(defs.items()) if len(owners)>1}
    return {"object_count":len(objects),"strong_duplicate_count":len(dup),
            "strong_duplicate_definitions":dup,
            "scope":"ET_REL strong global symbols only; weak/COMDAT, ABI, W^X and guest boot require final link/security review"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("objects",nargs="+",type=Path)
    p.add_argument("--report",type=Path)
    a=p.parse_args()
    try:
        result=analyze(a.objects)
        if a.report:
            a.report.parent.mkdir(parents=True,exist_ok=True)
            a.report.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
        print(json.dumps(result,sort_keys=True))
        return 1 if result["strong_duplicate_count"] else 0
    except (OSError,ValueError,subprocess.TimeoutExpired) as e:
        print("STRONG SYMBOL OWNER CHECK FAILED: "+str(e),file=sys.stderr)
        return 2

if __name__=="__main__":
    sys.exit(main())
