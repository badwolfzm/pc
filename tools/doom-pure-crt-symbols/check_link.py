#!/usr/bin/env python3
"""Real Doom/Warden relocatable closure: only abs and fabs."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

EXPECTED = frozenset(("abs", "fabs"))
SENSITIVE = frozenset(("system", "fopen", "fread", "fwrite", "malloc", "free"))
def symbols(output):
    found=set()
    for line in output.splitlines():
        parts=line.split()
        if len(parts)!=2 or parts[0]!="U": raise ValueError("unexpected nm output: "+line)
        if parts[1] in found: raise ValueError("duplicate undefined symbol")
        found.add(parts[1])
    return found
def verify(original, updated):
    if not EXPECTED.issubset(original): raise ValueError("baseline imports missing")
    if updated != original - EXPECTED: raise ValueError("unexpected imports changed")
    if not SENSITIVE.issubset(updated): raise ValueError("security-sensitive service unexpectedly linked")
    return {"status":"PASS","baseline_unresolved":len(original),
      "after_pure_crt_unresolved":len(updated),"removed_pure_symbols":sorted(EXPECTED),
      "unresolved_security_sensitive":sorted(SENSITIVE),
      "native_executable_built":False,"native_warden_boot_verified":False,
      "scope":"real pinned upstream Doom ET_REL partial link, not executable or guest boot"}
def run(root, object_file, out):
    before=root/"all-objects.o"
    if not before.is_file() or not object_file.is_file(): raise ValueError("missing genuine engine or helper object")
    linked=root/"with-pure-crt.o"
    subprocess.run(["ld","-r","-o",str(linked),str(before),str(object_file)],check=True,timeout=30)
    old=symbols(subprocess.check_output(["nm","-g","-u",str(before)],text=True))
    new=symbols(subprocess.check_output(["nm","-g","-u",str(linked)],text=True))
    result=verify(old,new)
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
    return result
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--engine-objects",type=Path,required=True)
    p.add_argument("--pure-crt-object",type=Path,required=True)
    p.add_argument("--report",type=Path,required=True)
    args=p.parse_args()
    try:
        print(json.dumps(run(args.engine_objects,args.pure_crt_object,args.report),sort_keys=True))
        return 0
    except (OSError,ValueError,subprocess.CalledProcessError,subprocess.TimeoutExpired) as e:
        print("PURE CRT LINK CHECK FAILED:",e,file=sys.stderr)
        return 1
if __name__=="__main__": raise SystemExit(main())
