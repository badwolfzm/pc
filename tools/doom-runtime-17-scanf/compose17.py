#!/usr/bin/env python3
"""Safely add one genuine Doom __isoc99_sscanf closure to the 16-provider ET_REL."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

EXPECTED = "__isoc99_sscanf"
PROTECTED = {"system", "fopen", "fclose", "fread", "fwrite",
             "fseek", "ftell", "remove", "rename", "fprintf", "fflush"}
PIN_SCANNER_SHA = "62dc9f73bee7753bd81d889a0e44ee8e95ce2220"


def execute(args):
    p = subprocess.run(args, check=True, capture_output=True, text=True)
    return p.stdout


def symbols(path):
    imported, defined = set(), set()
    for row in execute(["nm", "-P", "-g", str(path)]).splitlines():
        parts = row.split()
        if len(parts) < 2:
            raise ValueError("unrecognized nm line: " + row)
        name, kind = parts[:2]
        if kind == "U":
            imported.add(name)
        elif kind.upper() in {"T", "D", "B", "R", "W", "V", "C", "G", "S"}:
            defined.add(name)
        else:
            raise ValueError("unexpected nm symbol type: " + row)
    return imported, defined


def check(before_missing, after_missing, candidate_missing, candidate_defined, old_defined):
    before, after = set(before_missing), set(after_missing)
    if EXPECTED not in before:
        raise ValueError("scanner import was not genuinely unresolved")
    if candidate_missing:
        raise ValueError("candidate scanner contains new imports: " + repr(sorted(candidate_missing)))
    if candidate_defined != {EXPECTED}:
        raise ValueError("candidate must define exactly __isoc99_sscanf")
    if candidate_defined & old_defined:
        raise ValueError("duplicate scanner definition in 16-provider input")
    if before - after != {EXPECTED} or after - before:
        raise ValueError("import delta must remove one and add zero: " +
                         repr(sorted(before - after)) + " / " + repr(sorted(after - before)))
    protected = PROTECTED & before
    if not protected.issubset(after):
        raise ValueError("unexpected protected service closure")
    return {
        "resolved_only": [EXPECTED],
        "before_missing": len(before),
        "after_missing": len(after),
        "protected_still_unresolved": sorted(protected),
        "final_executable_built": False,
        "native_warden_boot_verified": False,
        "guest_512_mib_measured": False,
        "fps_measured": False,
        "output_kind": "ET_REL genuine Doom partial link only",
    }


def run(bundle, scanner_source, output):
    bundle_report = json.loads((bundle / "report.json").read_text())
    if bundle_report.get("native_warden_boot_verified") is not False:
        raise ValueError("untrusted bundle claims native boot")
    if len(bundle_report.get("exact_resolved_imports", [])) != 16:
        raise ValueError("expected strict 16-provider bundle")
    if bundle_report.get("upstream_commit") != "e257a09527062bfb986b6e5e0780839b6cbd5b71":
        raise ValueError("wrong pinned Doom engine")
    old = bundle / "genuine-doom-with-native-runtime.o"
    before, old_defined = symbols(old)
    if sorted(before) != sorted(bundle_report["remaining_imports"]):
        raise ValueError("bundle report disagrees with real object nm")
    output.mkdir(parents=True, exist_ok=True)
    candidate = output / "scanf.o"
    execute(["cc", "-std=c11", "-O2", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-fno-pic", "-fno-pie",
             "-c", str(scanner_source), "-o", str(candidate)])
    scanner_missing, scanner_defined = symbols(candidate)
    combined = output / "genuine-doom-runtime-17.o"
    if scanner_missing or scanner_defined != {EXPECTED} or scanner_defined & old_defined:
        raise ValueError("scanner symbol ownership or imports invalid")
    execute(["ld", "-r", "-o", str(combined), str(old), str(candidate)])
    after, _ = symbols(combined)
    result = check(before, after, scanner_missing, scanner_defined, old_defined)
    result["scanner_exact_commit"] = PIN_SCANNER_SHA
    result["scanner_source_sha256"] = hashlib.sha256(scanner_source.read_bytes()).hexdigest()
    result["real_engine_upstream_commit"] = bundle_report["upstream_commit"]
    result["original_bundle_object_sha256"] = hashlib.sha256(old.read_bytes()).hexdigest()
    result["combined_object_sha256"] = hashlib.sha256(combined.read_bytes()).hexdigest()
    result["remaining_symbols"] = sorted(after)
    (output / "report.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("bundle", "scanner-source", "output"):
        p.add_argument("--" + name, required=True, type=Path)
    args = p.parse_args()
    try:
        result = run(args.bundle, args.scanner_source, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError, KeyError) as exc:
        p.error(str(exc))
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
