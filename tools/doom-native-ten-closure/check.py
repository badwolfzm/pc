#!/usr/bin/env python3
"""Actual pinned Doom + Warden ET_REL closure: ten symbols, no unsafe masking."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = frozenset(("abs", "fabs", "strcasecmp", "strncasecmp", "strstr",
                       "strncpy", "malloc", "calloc", "realloc", "free"))
PROTECTED = frozenset(("system", "fopen", "fread", "fwrite", "fclose", "fseek",
                       "ftell", "remove", "rename"))

def verify(before, after):
    before, after = set(before), set(after)
    if not EXPECTED.issubset(before):
        raise ValueError("genuine upstream baseline lacks expected imports: "
                         + repr(sorted(EXPECTED-before)))
    if not PROTECTED.issubset(before):
        raise ValueError("genuine upstream baseline lacks expected protected imports")
    removed = before-after
    if removed != EXPECTED:
        raise ValueError("only ten reviewed symbols may disappear: "
                         + repr(sorted(removed ^ EXPECTED)))
    if after-before:
        raise ValueError("new external symbols: " + repr(sorted(after-before)))
    if not PROTECTED.issubset(after):
        raise ValueError("unsafe host file/process symbols disappeared")
    return {"removed_symbols":sorted(removed),"before_count":len(before),
            "after_count":len(after),"remaining_symbols":sorted(after),
            "protected_imports_still_unresolved":sorted(PROTECTED),
            "native_executable":False,"native_warden_boot":False,
            "classification":"genuine Doom x86-64 ET_REL linker diagnostic only"}

def load_gate():
    path=ROOT/"tools/doom-native-link-gate/gate.py"
    spec=importlib.util.spec_from_file_location("warden_gate_ten",path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def run(source,output):
    arena=ROOT/"tools/doom-freestanding-arena/arena.c"
    alias=ROOT/"tools/doom-native-heap-closure/heap_alias.c"
    pure=ROOT/"tools/doom-pure-crt-symbols/pure.c"
    if not all(p.is_file() for p in (arena,alias,pure)):
        raise ValueError("exact-SHA PR #113, #176 and #177 files must be staged")
    gate=load_gate()
    output.mkdir(parents=True,exist_ok=True)
    base=gate.run(source,output/"base")
    common=["cc",*gate.CFLAGS,"-I"+str(ROOT/"tools/doom-freestanding-arena")]
    objects=[]
    for name,path in (("pure",pure),("arena",arena),("alias",alias)):
        obj=output/(name+".o")
        gate.execute([*common,"-DDOOM_ARENA_BYTES=1048576","-c",str(path),"-o",str(obj)])
        objects.append(obj)
    combined=output/"doom-ten-closure.o"
    gate.execute(["ld","-r","-o",str(combined),str(output/"base/all-objects.o"),
                  *[str(obj) for obj in objects]])
    remaining=gate.missing_symbols(gate.execute(["nm","-g","-u",str(combined)]))
    result=verify(base["unresolved_symbols"],remaining)
    result.update({"upstream_sha":gate.PIN,"arena_bytes_static":1048576,
                   "arena_pr":113,"pure_crt_pr":177,"heap_wrapper_pr":176,
                   "combined_object_sha256":hashlib.sha256(combined.read_bytes()).hexdigest(),
                   "actual_heap_allocation_behavior_measured":False,
                   "guest_total_ram_measured":False,
                   "note":"Not an ELF executable; no entry/load/guest game, physical RAM, FPS or asset permission test."})
    (output/"report.json").write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
    print(json.dumps(result,sort_keys=True))
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    args=ap.parse_args()
    try:
        run(args.source,args.output)
        return 0
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as exc:
        print("TEN-SYMBOL CLOSURE FAILED: "+str(exc),file=sys.stderr)
        return 1

if __name__=="__main__":
    raise SystemExit(main())
