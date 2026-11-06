#!/usr/bin/env python3
"""Link pinned genuine Doom objects against nonoverlapping CRT providers.

Host-side ET_REL proof only; NEVER execute upstream Doom in this gate.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
REQUIRED=frozenset(("abs","fabs","strcasecmp","strncasecmp","strstr","strncpy",
                    "malloc","calloc","realloc","free"))
FORBIDDEN_HOST=frozenset(("system","fopen","fclose","fread","fwrite","fseek",
                          "ftell","remove","rename"))
EXPECTED_PROVIDERS={
    "pure":frozenset(("abs","fabs")),
    "extra":frozenset(("strcasecmp","strncasecmp","strstr","strncpy")),
    "heap":frozenset(("malloc","calloc","realloc","free")),
}

def verify(before,after,providers):
    b,a=set(before),set(after)
    if not REQUIRED.issubset(b):
        raise ValueError("missing initial imports: "+repr(sorted(REQUIRED-b)))
    if not FORBIDDEN_HOST.issubset(b):
        raise ValueError("expected protected host imports missing in baseline")
    if b-a != REQUIRED or a-b:
        raise ValueError("unexpected unresolved symbol delta: "+repr(sorted(b-a))+" / "+repr(sorted(a-b)))
    if not FORBIDDEN_HOST.issubset(a):
        raise ValueError("unsafe host I/O symbols have been silently resolved")
    owners={}
    for module, symbols in providers.items():
        for symbol in symbols:
            if symbol in owners:
                raise ValueError("duplicate strong definition "+symbol+": "+owners[symbol]+"/"+module)
            owners[symbol]=module
    for module,expected in EXPECTED_PROVIDERS.items():
        missing=expected-set(providers[module])
        if missing:
            raise ValueError("provider lacks expected symbols: "+module+" "+repr(sorted(missing)))
    if REQUIRED-set(owners):
        raise ValueError("expected providers missing: "+repr(sorted(REQUIRED-set(owners))))
    return {"original_unresolved":len(b),"remaining_unresolved":len(a),
            "removed":sorted(REQUIRED),"remaining":sorted(a),
            "protected_host_imports_still_unresolved":sorted(FORBIDDEN_HOST),
            "duplicate_global_definitions":False,
            "native_executable":False,"native_warden_boot":False}

def gate_module():
    path=ROOT/"tools/doom-native-link-gate/gate.py"
    spec=importlib.util.spec_from_file_location("doom_native_current",path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def defined_symbols(gate,obj):
    output=gate.execute(["nm","-g","--defined-only",str(obj)])
    symbols=set()
    for line in output.splitlines():
        match=re.fullmatch(r"\s*[0-9a-fA-F]+\s+([A-Z])\s+(\S+)\s*",line)
        if not match:
            raise ValueError("unexpected nm defined-symbol format: "+line)
        typ,name=match.groups()
        # Only strong global data/code definitions; ignore weak and local symbols.
        if typ not in ("W","V"):
            symbols.add(name)
    return symbols

def run(source,output):
    deps={
        "pure":ROOT/"tools/doom-pure-crt-symbols/pure.c",
        "extra":ROOT/"tools/doom-mini-crt/mini_extra.c",
        "arena":ROOT/"tools/doom-freestanding-arena/arena.c",
        "heap":ROOT/"tools/doom-native-heap-closure/heap_alias.c",
    }
    for name,p in deps.items():
        if not p.is_file():
            raise ValueError(name+" dependency not staged: "+str(p))
    gate=gate_module()
    output.mkdir(parents=True,exist_ok=True)
    baseline=gate.run(source,output/"baseline")
    flags=["cc",*gate.CFLAGS,"-I"+str(ROOT/"tools/doom-freestanding-arena"),
           "-DDOOM_ARENA_BYTES=1048576"]
    modules={}
    providers={}
    for name,path in deps.items():
        obj=output/(name+".o")
        gate.execute([*flags,"-c",str(path),"-o",str(obj)])
        modules[name]=obj
        providers[name]=defined_symbols(gate,obj)
    # Verify individual component surfaces *before* invoking ld -r.
    verify_provider_defs(providers)
    obj=output/"all-current-providers.o"
    gate.execute(["ld","-r","-o",str(obj),str(output/"baseline/all-objects.o"),
                 *[str(modules[k]) for k in ("pure","extra","arena","heap")]])
    after=gate.missing_symbols(gate.execute(["nm","-g","-u",str(obj)]))
    result=verify(baseline["unresolved_symbols"],after,providers)
    result.update({
        "pinned_upstream_sha":gate.PIN,
        "engine_upstream_translation_units":baseline["real_upstream_source_count"],
        "combined_rel_sha256":hashlib.sha256(obj.read_bytes()).hexdigest(),
        "providers":{name:sorted(symbols) for name,symbols in providers.items()},
        "arena_bytes_static":1048576,
        "scope":"Real pinned Doom x86-64 freestanding ET_REL linker only; no native guest QEMU, linker PT_LOAD, permission proof, RAM or FPS",
    })
    (output/"report.json").write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
    print(json.dumps(result,sort_keys=True))
    return result

def verify_provider_defs(providers):
    owners={}
    for module,syms in providers.items():
        for s in syms:
            if s in owners:
                raise ValueError("duplicate strong global definition: "+s+" in "+owners[s]+" and "+module)
            owners[s]=module
    for module,need in EXPECTED_PROVIDERS.items():
        if not need.issubset(providers[module]):
            raise ValueError(module+" lacks "+repr(sorted(need-providers[module])))
    return True

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    try:
        run(a.source,a.output)
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as e:
        print("CANONICAL DOOM LINK FAILED: "+str(e),file=sys.stderr)
        return 1
    return 0

if __name__=="__main__":
    sys.exit(main())
