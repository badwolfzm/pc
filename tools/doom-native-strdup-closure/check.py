#!/usr/bin/env python3
"""Source-backed genuine Doom ET_REL allocator and strdup link closure."""
import argparse, importlib.util, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def verify(before,after):
    a,b=set(before),set(after)
    if "strdup" not in a or "strdup" in b:raise ValueError("strdup closure not proven")
    if b-a:raise ValueError("new unresolved imports: "+str(sorted(b-a)))
    if any(x in a and x not in b for x in ("system","fopen","fread","fwrite")):
        raise ValueError("sensitive host imports unexpectedly suppressed")
    return sorted(a-b)
def run(source,out):
    heap=load(ROOT/"tools/doom-native-heap-closure/close.py","heapclosure")
    gate=heap.load_gate()
    out.mkdir(parents=True,exist_ok=True)
    prior=heap.run(source,out/"heap",1048576)
    obj=out/"strdup.o"
    gate.execute(["cc",*gate.CFLAGS,"-c",str(ROOT/"tools/doom-native-strdup-closure/strdup.c"),"-o",str(obj)])
    unit=gate.missing_symbols(gate.execute(["nm","-g","-u",str(obj)]))
    if not set(unit).issubset({"malloc","strlen","memcpy"}):raise ValueError("unexpected implementation imports "+str(unit))
    linked=out/"combined.o"
    gate.execute(["ld","-r","-o",str(linked),str(out/"heap/engine-with-heap.o"),str(obj)])
    after=gate.missing_symbols(gate.execute(["nm","-g","-u",str(linked)]))
    removed=verify(prior["still_unresolved"],after)
    result={"baseline_unresolved":len(prior["still_unresolved"]),"remaining_unresolved":len(after),
            "removed":removed,"object_imports":unit,"native_executable_built":False,
            "warden_boot_verified":False,"guest_512m_verified":False,
            "scope":"Real Doom plus Warden and heap relocatable link; not an ELF executable"}
    (out/"strdup-report.json").write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
    return result
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--source",type=Path,required=True);p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    try:print(json.dumps(run(a.source,a.output),indent=2,sort_keys=True))
    except (ValueError,RuntimeError,OSError) as e:p.error(str(e))
