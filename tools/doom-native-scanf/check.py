#!/usr/bin/env python3
"""Verify exact one-symbol closure with actual genuine Doom ET_REL."""
import argparse
import pathlib
import subprocess

def nm(path):
    result = subprocess.run(["nm", "-g", "-u", str(path)], check=True,
                            capture_output=True, text=True).stdout
    return {line.strip().split()[-1] for line in result.splitlines() if line.strip()}

def verify(original, provider, combined):
    before = nm(original)
    after = nm(combined)
    generated = nm(provider)
    if generated:
        raise ValueError("provider introduces external dependencies: " + repr(sorted(generated)))
    if "__isoc99_sscanf" not in before:
        raise ValueError("actual upstream import absent; cannot claim closure")
    if before - after != {"__isoc99_sscanf"} or after - before:
        raise ValueError("unexpected import changes: removed=" + repr(sorted(before - after)) +
                         " added=" + repr(sorted(after - before)))
    for protected in ("system", "fopen", "fread", "fprintf", "printf"):
        if protected in before and protected not in after:
            raise ValueError("protected host operation incorrectly closed: " + protected)
    return len(before), len(after)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for item in ("original", "provider", "combined"):
        p.add_argument("--" + item, type=pathlib.Path, required=True)
    a = p.parse_args()
    try:
        before, after = verify(a.original, a.provider, a.combined)
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        p.error(str(exc))
    print(f"PASS real Doom exact closure: {before} -> {after} (ET_REL only; not boot)")

if __name__ == "__main__":
    main()
