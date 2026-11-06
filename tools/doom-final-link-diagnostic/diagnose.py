#!/usr/bin/env python3
"""Attempt genuine final ELF link; fail closed without inventing host ABI stubs."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

LIMIT = 64 * 1024 * 1024
ENTRY = "doomgeneric_Create"
REQUIRED_HOST_IMPORTS = {"system", "fopen", "fclose", "fread", "fwrite",
                         "fseek", "ftell", "remove", "rename"}

class DiagnosticError(ValueError):
    pass

def nm(path, undefined=False):
    args = ["nm", "-P", "-g", "-u" if undefined else "--defined-only", str(path)]
    result = subprocess.run(args, check=True, text=True, capture_output=True, timeout=30)
    symbols = set()
    for line in result.stdout.splitlines():
        words = line.split()
        if len(words) < 2 or (undefined and words[1] != "U"):
            raise DiagnosticError("unexpected nm record")
        if words[0] in symbols:
            raise DiagnosticError("duplicate global nm entry")
        symbols.add(words[0])
    return symbols

def diagnose(source, output, report, expected_sha256=None):
    if not source.is_file() or source.is_symlink() or source.stat().st_size > LIMIT:
        raise DiagnosticError("missing, symlink or oversized link object")
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected_sha256 is not None and digest != expected_sha256:
        raise DiagnosticError("input object SHA-256 mismatch")
    missing = nm(source, True)
    defined = nm(source, False)
    if ENTRY not in defined:
        raise DiagnosticError("real Doom entry point missing")
    if not REQUIRED_HOST_IMPORTS.issubset(missing):
        raise DiagnosticError("protected file/process imports absent: unsafe provider or wrong input")
    if not missing:
        raise DiagnosticError("input already complete; not a blocker diagnostic")
    if output.exists():
        raise DiagnosticError("refusing to overwrite existing final executable")
    cmd = ["ld", "-nostdlib", "-static", "--no-undefined", "-e", ENTRY,
           "-Ttext", "0x800000", "-o", str(output), str(source)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    # A successful final executable is a separate milestone with different
    # security review and tests, never acceptable for this negative gate.
    if result.returncode == 0 or output.exists():
        raise DiagnosticError("unexpected linker output; examine before making any boot claim")
    stderr = result.stderr[:16384]
    # A failure caused only by missing linker tooling/flags is not a useful
    # unresolved-ABI result.
    if "undefined reference" not in stderr:
        raise DiagnosticError("link failed without unresolved-reference evidence: " + stderr[:500])
    result_json = {
        "input_type": "ELF relocatable object",
        "input_sha256": digest,
        "entry_symbol": ENTRY,
        "link_attempted": True,
        "final_executable_built": False,
        "linker_exit_code": result.returncode,
        "unresolved_import_count": len(missing),
        "unresolved_imports": sorted(missing),
        "protected_host_services_unresolved": sorted(REQUIRED_HOST_IMPORTS),
        "linker_stderr_excerpt": stderr,
        "native_warden_boot_verified": False,
        "one_vcpu_512m_measured": False,
        "fps_measured": False,
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result_json, indent=2, sort_keys=True) + "\n")
    print(json.dumps({k: result_json[k] for k in ("link_attempted", "final_executable_built",
        "unresolved_import_count", "protected_host_services_unresolved")}, sort_keys=True))
    return result_json

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("combined_object", type=pathlib.Path)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--report", type=pathlib.Path, required=True)
    p.add_argument("--sha256")
    a = p.parse_args()
    try:
        diagnose(a.combined_object, a.output, a.report, a.sha256)
    except (DiagnosticError, OSError, subprocess.SubprocessError) as e:
        print("final-link diagnostic FAILED: " + str(e), file=sys.stderr)
        sys.exit(1)
