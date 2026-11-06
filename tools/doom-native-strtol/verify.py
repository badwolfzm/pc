#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys

def undefined(path):
    result=subprocess.check_output(["nm","-P","-g","-u",str(path)],text=True)
    symbols=set()
    for line in result.splitlines():
        f=line.split()
        if len(f)!=2 or f[1]!="U" or f[0] in symbols:
            raise ValueError("invalid nm output: "+line)
        symbols.add(f[0])
    return symbols

def run(before,after,output):
    old=undefined(before);new=undefined(after)
    if "strtol" not in old or new != old-{"strtol"}:
        raise ValueError("strtol not exclusively resolved")
    if "__errno_location" not in new:
        raise ValueError("errno provider unexpectedly resolved")
    dangerous={"system","fopen","fread","fwrite","remove","rename"}
    if not dangerous.issubset(new):
        raise ValueError("sensitive imports unexpectedly resolved")
    report={"original_missing":len(old),"remaining_missing":len(new),
            "resolved":["strtol"],"remaining_symbols":sorted(new),
            "native_guest_built":False,"boot_verified":False,
            "errno_requires_guest_provider":True}
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    print(json.dumps(report,sort_keys=True))
if __name__=="__main__":
    if len(sys.argv)!=4:raise SystemExit("usage: verify.py old.o new.o report.json")
    run(pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]),pathlib.Path(sys.argv[3]))
