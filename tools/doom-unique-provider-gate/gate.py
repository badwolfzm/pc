#!/usr/bin/env python3
"""Real pinned Doom + Warden runtime provider uniqueness and link-closure gate.

This validates a candidate ET_REL assembly, never claims a bootable guest.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
PIN_DOOM="e257a09527062bfb986b6e5e0780839b6cbd5b71"
PIN_PURE="254b941d4c76e7dd91fb21354b1679f9291a74ab"
PIN_STRINGS="6d6237735fad9b0f01f5e7e0064656560df3fc31"
PIN_HEAP="e0b20c35a73a6e590c0b8dbe62910342f3d07527"
PIN_ARENA="315cc925ebf67f67d883bfa4d507ebcbb6b84c10"
EXPECTED=frozenset(("abs","fabs","strcasecmp","strncasecmp","strncpy","strstr",
                    "malloc","calloc","realloc","free"))
PROTECTED=frozenset(("system","fopen","fclose","fread","fwrite","fseek","ftell",
                     "remove","rename"))
PURE_ONLY=frozenset(("abs","fabs"))
STRING_ONLY=frozenset(("strcasecmp","strncasecmp","strncpy","strstr"))
HEAP_ONLY=frozenset(("malloc","calloc","realloc","free"))
WEAK=frozenset(("W","V","w","v"))

def defined_strong(nm_output):
    """Parse nm -P -g --defined-only; only strong exported functions/data count."""
    names=set()
    for line in nm_output.splitlines():
        fields=line.split()
        if len(fields)<2 or len(fields)>4 or len(fields[1])!=1:
            raise ValueError("unexpected defined nm record: "+line)
        name,kind=fields[:2]
        if kind in ("U","u","?"):
            raise ValueError("undefined or unknown record in defined-only output")
        if kind in WEAK: continue
        if name in names: raise ValueError("duplicate within object: "+name)
        names.add(name)
    return names

def providers(objects):
    """Reject collisions among baseline and selected add-on objects."""
    owner={}
    for label, names in objects:
        for name in names:
            if name in owner:
                raise ValueError("duplicate strong provider for "+name+": "+owner[name]+" and "+label)
            owner[name]=label
    return owner

def verify(before,after,own):
    b=set(before);a=set(after)
    if not EXPECTED.issubset(b): raise ValueError("baseline missing targets "+repr(sorted(EXPECTED-b)))
    if not PROTECTED.issubset(b): raise ValueError("baseline protected imports absent")
    if b-a != EXPECTED: raise ValueError("unexpected removals "+repr(sorted((b-a)^EXPECTED)))
    if a-b: raise ValueError("new undefined symbols "+repr(sorted(a-b)))
    if not PROTECTED.issubset(a): raise ValueError("protected host-service imports resolved")
    for sym in EXPECTED:
        if sym not in own: raise ValueError("target lacks provider: "+sym)
    for syms,label in ((PURE_ONLY,"pure"),(STRING_ONLY,"strings"),(HEAP_ONLY,"heap-alias")):
        for name in syms:
            if own[name] != label:
                raise ValueError("unexpected provider for "+name+": "+own[name])
    return {
        "status":"PASS","baseline_undefined_count":len(b),
        "combined_undefined_count":len(a),
        "resolved_symbols":sorted(EXPECTED),
        "protected_imports_still_unresolved":sorted(PROTECTED),
        "sources":{"upstream_doom":PIN_DOOM,"merged_pure":PIN_PURE,
                   "candidate_string_extras_closed_unmerged_pr175":PIN_STRINGS,
                   "draft_heap_alias":PIN_HEAP,"draft_arena":PIN_ARENA},
        "selected_providers":{s:own[s] for s in sorted(EXPECTED)},
        "build_kind":"genuine pinned Doom + Warden + selected runtime ET_REL partial link",
        "fully_merged_runtime":False,"native_executable":False,
        "warden_guest_boot_verified":False,"guest_512m_usage_verified":False
    }

def process(args):
    if not args.source.is_dir(): raise ValueError("upstream directory absent")
    included={
        "pure":ROOT/"tools/doom-pure-crt-symbols/pure.c",
        "strings":ROOT/"tools/doom-mini-crt/mini_extra.c",
        "arena":ROOT/"tools/doom-freestanding-arena/arena.c",
        "heap-alias":ROOT/"tools/doom-native-heap-closure/heap_alias.c"
    }
    for path in included.values():
        if not path.is_file() or path.is_symlink(): raise ValueError("missing or symlinked runtime source: "+str(path))
    source_digest={k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in included.items()}
    lib=ROOT/"tools/doom-native-link-gate/gate.py"
    spec=importlib.util.spec_from_file_location("real_doom_gate",lib)
    gate=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    if gate.PIN!=PIN_DOOM: raise ValueError("actual Doom source pin mismatch")
    out=args.output
    out.mkdir(parents=True,exist_ok=True)
    baseline=gate.run(args.source,out/"baseline")
    if baseline["native_executable_built"] or baseline["native_warden_boot_verified"]:
        raise ValueError("unexpected upstream provenance")
    extras=[]
    for label,src in included.items():
        obj=out/(label+".o")
        flags=[*gate.CFLAGS,"-DDOOM_ARENA_BYTES=1048576",
               "-I"+str(ROOT/"tools/doom-freestanding-arena")]
        gate.execute(["cc",*flags,"-c",str(src),"-o",str(obj)])
        extras.append((label,obj))
    base=out/"baseline/all-objects.o"
    allfiles=[("engine-and-warden",base),*extras]
    owners=providers([(label,defined_strong(gate.execute(["nm","-P","-g","--defined-only",str(path)]))) for label,path in allfiles])
    combined=out/"candidate-runtime.o"
    gate.execute(["ld","-r","-o",str(combined),*[str(path) for _,path in allfiles]])
    missing=gate.missing_symbols(gate.execute(["nm","-g","-u",str(combined)]))
    result=verify(baseline["unresolved_symbols"],missing,owners)
    result["included_source_sha256"]=source_digest
    result["combined_et_rel_sha256"]=hashlib.sha256(combined.read_bytes()).hexdigest()
    result["static_arena_candidate_bytes"]=1048576
    result["remaining_undefined"]=missing
    result["note"]="Candidate source composition, NOT merged deployment or native ELF/guest. No host-service substitution."
    (out/"report.json").write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("status","baseline_undefined_count",
          "combined_undefined_count","resolved_symbols","build_kind")},sort_keys=True))
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    try:process(a)
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as exc:
        print("UNIQUE PROVIDER GATE FAILED: "+str(exc),file=sys.stderr)
        return 1
    return 0
if __name__=="__main__":raise SystemExit(main())
