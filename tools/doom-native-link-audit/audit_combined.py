#!/usr/bin/env python3
"""Size and unresolved-import audit of the *actual combined* freestanding ET_REL."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from audit import sections, unresolved, evaluate, PIN

# These imports must never silently inherit Linux host privileges when
# linking into a Warden ring-3 guest. Replacements need capability policies.
DANGEROUS_HOST_IMPORTS = frozenset({
    'system', 'popen', 'fork', 'execve', 'dlopen', 'dlsym',
    'fopen', 'freopen', 'remove', 'rename', 'mkdir', 'socket', 'connect',
    'open', 'openat', 'unlink', 'chmod', 'chown',
})

def inspect(object_path):
    if not object_path.is_file() or object_path.stat().st_size > 128*1024*1024:
        raise ValueError("missing or oversized combined object")
    header = subprocess.run(["readelf", "-W", "-h", str(object_path)],
                            capture_output=True, text=True, check=True, timeout=15).stdout
    if "Type:                              REL (Relocatable file)" not in header:
        raise ValueError("requires an actual relocatable ELF")
    if "Machine:                           Advanced Micro Devices X86-64" not in header:
        raise ValueError("requires an x86-64 ELF")
    se = subprocess.run(["readelf", "-W", "-S", str(object_path)],
                        capture_output=True, text=True, check=True, timeout=15).stdout
    nm = subprocess.run(["nm", "-P", "-g", "-u", str(object_path)],
                        capture_output=True, text=True, check=True, timeout=15).stdout
    result = evaluate(sections(se), unresolved(nm))
    result["object_kind"] = "real x86_64 relocatable ET_REL engine + Warden adapters"
    result["native_warden_executable_built"] = False
    result["native_warden_boot_verified"] = False
    result["host_privilege_imports_require_policy"] = sorted(set(result["upstream_engine_unresolved_symbols"]) & DANGEROUS_HOST_IMPORTS)
    result["host_privilege_imports_policy_approved"] = False
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--combined", type=Path, required=True)
    parser.add_argument("--engine-report", type=Path, required=True)
    parser.add_argument("--gate-report", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    a=parser.parse_args()
    try:
        engine=json.loads(a.engine_report.read_text())
        gate=json.loads(a.gate_report.read_text())
        if engine.get("pinned_commit") != PIN or gate.get("upstream_commit") != PIN:
            raise ValueError("mismatched pinned engine revisions")
        if gate.get("real_upstream_source_count") != 79 or gate.get("warden_component_count") != 5:
            raise ValueError("unexpected combined source inventory")
        result=inspect(a.combined)
        if result["upstream_engine_unresolved_symbols"] != gate.get("unresolved_symbols"):
            raise ValueError("combined ELF unresolved symbols differ from the actual gate")
        result["upstream_commit"]=PIN
        result["upstream_units_in_combined"]=79
        result["warden_component_units_in_combined"]=5
        result["engine_only_section_bytes"]=engine["allocatable_section_bytes"]
        result["combined_minus_engine_only_section_bytes"]=result["allocatable_section_bytes"]-engine["allocatable_section_bytes"]
        result["comparison_warning"]="Combined omits the upstream stdio WAD backend and adds five Warden units; subtraction is not strictly the five adapters' size. No final PT_LOAD alignment/placement, game heap, WAD, complete CRT, physical guest memory or runtime FPS."
        result["missing_imports"]=gate["unresolved_symbols"]
        result["safe_to_bind_unrestricted_host_libc"] = False
        a.out.parent.mkdir(parents=True,exist_ok=True)
        a.out.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
        print(json.dumps({k:result[k] for k in ("allocatable_section_bytes","allocatable_file_backed_bytes","allocatable_zero_fill_bytes","upstream_engine_unresolved_count","combined_minus_engine_only_section_bytes")},sort_keys=True))
        return 0
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        print("COMBINED ELF AUDIT FAILED: "+str(exc), file=sys.stderr)
        return 1

if __name__=="__main__":
    sys.exit(main())
