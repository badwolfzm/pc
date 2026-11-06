#!/usr/bin/env python3
"""Pin native Doom no-shell decision to actual upstream error dialog call sites."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
PROTECTED = frozenset({"fopen","fclose","fread","fwrite","fseek","ftell",
                       "remove","rename"})
TARGET = "system"


def symbols(obj):
    out = subprocess.run(["nm","-P","-g",str(obj)],capture_output=True,
                         text=True,check=True).stdout
    defined, undefined = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 2 or len(parts) > 4:
            raise ValueError("invalid nm global record")
        name, typ = parts[:2]
        if typ == "U": undefined.add(name)
        elif typ in ("T","D","B","R","C","A","W","V"): defined.add(name)
        else: raise ValueError("unrecognized symbol type: " + line)
    return defined, undefined


def check(before, after, provider, errno):
    defined, missing = symbols(provider)
    err_defs, err_missing = symbols(errno)
    if defined != {TARGET} or missing != {"__errno_location"}:
        raise ValueError("no-shell provider unexpected exports or imports")
    if err_defs != {"__errno_location"} or err_missing:
        raise ValueError("errno must be isolated single-thread candidate")
    _, old = symbols(before)
    _, new = symbols(after)
    if TARGET not in old or new != old - {TARGET}:
        raise ValueError("system must be only original removed import")
    if not PROTECTED.issubset(new):
        raise ValueError("host filesystem imports masked")
    return {"baseline_unresolved": len(old), "remaining_unresolved":len(new),
            "closed_exactly":[TARGET], "host_file_imports_still_missing":sorted(PROTECTED)}


def validate_source(root):
    original = subprocess.run(["git", "-C",str(root),"rev-parse","HEAD"],
                              check=True,capture_output=True,text=True).stdout.strip()
    if original != PIN: raise ValueError("unexpected genuine Doom upstream SHA")
    import sys
    audit_path = Path(__file__).resolve().parents[1] / "doom-native-io-callsites/audit.py"
    spec = importlib.util.spec_from_file_location("native_io_audit", audit_path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    data = m.run(root)
    actual = [x for x in data["candidates"] if x["function"] == TARGET]
    if len(actual) != 2 or [x["file"] for x in actual] != ["i_system.c"]*2:
        raise ValueError("system callsites changed: audit not sufficient")
    # Lock specifically to optional error-GUI flow; inspect in source by name.
    source = (root / "i_system.c").read_text()
    if ("static int ZenityAvailable(void)" not in source or
        "static int ZenityErrorBox(char *message)" not in source or
        "return system(ZENITY_BINARY" not in source or
        "result = system(errorboxpath)" not in source):
        raise ValueError("real system callsite flow changed")
    return [{"file":x["file"],"line":x["line"],"source_line_sha256":x["line_sha256"]}
            for x in actual]


def run(source, before, after, provider, errno, output):
    calls = validate_source(source)
    result = check(before, after, provider, errno)
    result.update({"upstream_commit":PIN,"system_callsites":calls,
        "native_system_command_processor_available":False,
        "fallback":"system(NULL) returns 0; nonnull returns -1, errno ENOSYS",
        "combined_sha256":hashlib.sha256(after.read_bytes()).hexdigest(),
        "booted_real_doom_in_warden":False,"measured_fps":None,"measured_ram_mib":None})
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
    print(json.dumps({"before":result["baseline_unresolved"],"after":result["remaining_unresolved"],
        "closed_exactly":result["closed_exactly"],"system_callsites":calls},sort_keys=True))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("source","before","after","provider","errno","output"):
        p.add_argument(name,type=Path)
    a=p.parse_args()
    run(a.source,a.before,a.after,a.provider,a.errno,a.output)


if __name__=="__main__": main()
