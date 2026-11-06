#!/usr/bin/env python3
"""Validate genuine Doom relocatable link closed exactly read-only FILE imports."""
import json
import pathlib
import subprocess
import argparse
TARGETS = {"fopen", "fclose", "fread", "fseek", "ftell"}
EXTRA = {"warden_stdio_grant_readonly", "warden_stdio_revoke"}
KEEP = {"system", "fwrite", "remove", "rename", "mkdir"}
def symbols(path):
    text = subprocess.check_output(["nm", "-P", "-g", str(path)], text=True)
    defs, undef = set(), set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 2:
            raise ValueError("unexpected nm line: "+line)
        if parts[1] == "U":
            undef.add(parts[0])
        elif parts[1].upper() in {"T","D","B","R","C","W","V","G","S"}:
            defs.add(parts[0])
        else:
            raise ValueError("unexpected nm type: "+line)
    return defs,undef
def inspect(before, after, provider):
    base_def, old = symbols(before)
    new_def, new = symbols(after)
    exported, imported = symbols(provider)
    if exported != TARGETS | EXTRA or imported:
        raise ValueError("provider changed exports/imports")
    if not TARGETS.issubset(old) or TARGETS & base_def:
        raise ValueError("original Doom FILE imports missing or already provided")
    if new != old-TARGETS:
        raise ValueError("unexpected additions or masked imports")
    if not KEEP.issubset(new):
        raise ValueError("protected host write/process calls must stay unresolved")
    if new_def != base_def | TARGETS | EXTRA:
        raise ValueError("unexpected symbol owners")
    return {"exactly_resolved":sorted(TARGETS),"remaining_unresolved":sorted(new),
            "native_warden_boot_verified":False,"actual_kernel_grant_verified":False}
if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for n in ("before","after","provider","output"):
        p.add_argument(n,type=pathlib.Path)
    a=p.parse_args()
    result=inspect(a.before,a.after,a.provider)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
    print(json.dumps(result,sort_keys=True))
