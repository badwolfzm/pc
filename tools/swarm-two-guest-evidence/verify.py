#!/usr/bin/env python3
"""Validate bounded two-node Warden run manifests; not a QEMU attestation."""
import argparse
import hashlib
import json
import re
from pathlib import Path

REQUIRED = {"node_id","job_id","vcpu","ram_mib","kernel_sha256","serial_path","serial_sha256","guest_exit_code","elapsed_ms"}
MAX_BYTES = 1048576

def verify(manifest: Path):
    try:
        if manifest.is_symlink() or manifest.stat().st_size > MAX_BYTES:
            return ["manifest is symlink or oversized"]
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        return [f"invalid manifest: {exc}"]
    if not isinstance(data, dict) or set(data) != {"runs"} or not isinstance(data["runs"], list) or len(data["runs"]) != 2:
        return ["exactly two runs required"]
    errors, nodes, jobs, paths = [], set(), set(), set()
    root = manifest.parent.resolve()
    for i, run in enumerate(data["runs"]):
        tag = f"runs[{i}]"
        if not isinstance(run, dict) or set(run) != REQUIRED:
            errors.append(f"{tag}: invalid schema")
            continue
        for field, seen in (("node_id",nodes),("job_id",jobs)):
            value = run[field]
            if not isinstance(value,str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}",value) or value in seen:
                errors.append(f"{tag}: invalid/duplicate {field}")
            else:
                seen.add(value)
        for field, expected in (("vcpu",1),("guest_exit_code",0)):
            if type(run[field]) is not int or run[field] != expected:
                errors.append(f"{tag}: invalid {field}")
        if type(run["ram_mib"]) is not int or not 1 <= run["ram_mib"] <= 512:
            errors.append(f"{tag}: RAM exceeds budget")
        if type(run["elapsed_ms"]) is not int or not 0 < run["elapsed_ms"] <= 86400000:
            errors.append(f"{tag}: invalid elapsed time")
        for field in ("kernel_sha256","serial_sha256"):
            value = run[field]
            if not isinstance(value,str) or not re.fullmatch("[0-9a-f]{64}",value):
                errors.append(f"{tag}: invalid {field}")
        relative = run["serial_path"]
        if not isinstance(relative,str) or not relative or Path(relative).is_absolute() or ".." in Path(relative).parts:
            errors.append(f"{tag}: unsafe evidence path")
            continue
        try:
            target = root / relative
            resolved = target.resolve(strict=True)
            if not resolved.is_relative_to(root) or not resolved.is_file() or target.is_symlink():
                raise ValueError("unsafe evidence target")
            if resolved in paths:
                raise ValueError("reused evidence target")
            paths.add(resolved)
            if resolved.stat().st_size > MAX_BYTES:
                raise ValueError("oversized evidence")
            with resolved.open("rb") as stream:
                digest = hashlib.sha256(stream.read(MAX_BYTES+1)).hexdigest()
            if digest != run["serial_sha256"]:
                raise ValueError("evidence digest mismatch")
        except (OSError,ValueError) as exc:
            errors.append(f"{tag}: {exc}")
    return errors

def main():
    p = argparse.ArgumentParser()
    p.add_argument("manifest",type=Path)
    errors = verify(p.parse_args().manifest)
    for error in errors:
        print("FAIL:",error)
    if not errors:
        print("PASS: bounded evidence manifest; authenticity and QEMU execution NOT established")
    return int(bool(errors))

if __name__ == "__main__":
    raise SystemExit(main())
