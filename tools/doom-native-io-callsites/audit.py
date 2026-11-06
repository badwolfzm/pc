#!/usr/bin/env python3
"""Locate security-sensitive host API call candidates in PINNED genuine Doom C.
A lexical inventory, not a C parser or proof of reachability/safe replacement.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[2]
PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
SENSITIVE = ("system", "fopen", "fclose", "fread", "fwrite", "fseek",
             "ftell", "remove", "rename", "mkdir")
CALL = re.compile(r"\b(" + "|".join(SENSITIVE) + r")\s*\(")

def mask_noncode(s):
    """Remove comments and literal bodies, retaining newlines and offsets."""
    chars=list(s)
    i=0
    while i<len(s):
        if s.startswith("//",i):
            j=s.find("\n",i);j=len(s) if j<0 else j
            for k in range(i,j):chars[k]=" "
            i=j
        elif s.startswith("/*",i):
            j=s.find("*/",i+2)
            if j<0:raise ValueError("unterminated C comment")
            j+=2
            for k in range(i,j):
                if chars[k]!="\n":chars[k]=" "
            i=j
        elif s[i] in ('"',"'"):
            quote=s[i];j=i+1;found=False
            while j<len(s):
                if s[j]=="\\":j+=2;continue
                if s[j]==quote:found=True;j+=1;break
                j+=1
            if not found:raise ValueError("unterminated C literal")
            for k in range(i,min(j,len(s))):
                if chars[k]!="\n":chars[k]=" "
            i=j
        else:i+=1
    return "".join(chars)

def candidates(source, filename):
    masked=mask_noncode(source)
    lines=source.splitlines()
    found=[]
    for m in CALL.finditer(masked):
        line=masked.count("\n",0,m.start())+1
        original=lines[line-1].strip()
        found.append({"function":m.group(1),"file":filename,"line":line,
                      "line_sha256":hashlib.sha256(original.encode()).hexdigest()})
    return found

def source_list(directory):
    spec=importlib.util.spec_from_file_location("doom_inventory",ROOT/"tools/doom-upstream-compile/inventory.py")
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    included=module.object_list((directory/"Makefile.soso").read_text())
    return sorted(x for x in included if x!="w_file_stdc.c")

def run(directory):
    sha=subprocess.check_output(["git","-C",str(directory),"rev-parse","HEAD"],text=True).strip()
    if sha!=PIN:raise ValueError("unexpected real Doom revision "+sha)
    files=source_list(directory)
    if len(files)!=79:raise ValueError("unexpected true engine source count")
    hits=[]
    for filename in files:
        path=directory/filename
        if not path.is_file() or path.is_symlink():raise ValueError("missing or symlink source "+filename)
        hits.extend(candidates(path.read_text(errors="replace"),filename))
    hits.sort(key=lambda x:(x["file"],x["line"],x["function"]))
    groups={name:[x for x in hits if x["function"]==name] for name in SENSITIVE}
    if not groups["system"] or not groups["fopen"]:
        raise ValueError("expected high-risk system/fopen call sites not identified")
    return {"scope":"lexical C source call candidates, NOT reachability or security audit",
            "upstream_revision":PIN,"source_count":len(files),
            "call_candidates_count":len(hits),
            "call_candidates_by_function":{k:len(v) for k,v in groups.items()},
            "candidates":hits,"native_wad_io_reviewed":False,
            "native_warden_booted":False,
            "recommendation":"Review each candidate and transitive call graph. Never emulate shell/file APIs with unrestricted host access or fake success stubs."}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    try:
        report=run(a.source)
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
        print(json.dumps({k:v for k,v in report.items() if k!="candidates"},indent=2))
    except (OSError,ValueError,subprocess.CalledProcessError) as e:p.error(str(e))
if __name__=="__main__":main()
