#!/usr/bin/env python3
"""Fail-closed ephemeral 512 MiB/smp=1 override for Warden's existing Pong test.

Run in a disposable CI checkout only. This does not touch the kernel or commit
a change to the owner-controlled pong.sh.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

SOURCE = "-machine pc,accel=\"$ACCEL\" -m 128M \"${DISP[@]}\""
TARGET = "-machine pc,accel=\"$ACCEL\" -m 512M -smp 1 \"${DISP[@]}\""

def transform(script: str) -> str:
    if script.count(SOURCE) != 1:
        raise ValueError("expected exactly one pinned QEMU memory stanza")
    if "-smp" in script or re.search(r"(?<!\w)-m\s+512M", script):
        raise ValueError("unexpected competing QEMU CPU/RAM setting")
    if script.count("qemu-system-x86_64") != 1:
        raise ValueError("unexpected QEMU invocation count")
    output = script.replace(SOURCE, TARGET)
    if output.count(TARGET) != 1 or SOURCE in output:
        raise ValueError("failed to produce exact 512M one-vCPU QEMU command")
    return output

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("script", type=Path)
    p.add_argument("--manifest", type=Path, required=True)
    args = p.parse_args()
    try:
        original = args.script.read_text()
        updated = transform(original)
        digest = hashlib.sha256(original.encode()).hexdigest()
        args.script.write_text(updated)
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps({
            "profile": "native-pong-qemu-512m-smp1",
            "guest_ram_bytes": 512 * 1024 * 1024, "guest_vcpus": 1,
            "source_script_sha256": digest, "modified_script_sha256":
                hashlib.sha256(updated.encode()).hexdigest(),
            "qemu_option": "-m 512M -smp 1",
            "note": "ephemeral CI script override only; does not change owner Pong script"
        }, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        print("NATIVE512 PREPARE FAILED:", exc, file=sys.stderr)
        return 1
    print("Native Warden Pong QEMU profile: -m 512M -smp 1")
    return 0

if __name__ == "__main__":
    sys.exit(main())
