#!/usr/bin/env python3
"""Strict *data-only* QEMU argv profile for an experimental Warden Doom run.

Never executes QEMU. This is not a sandbox, executable-provenance check, or
guest attestation. Reject every unreviewed extra command-line option.
"""
import json
import pathlib
import sys

ALLOWED_ACCEL = {"tcg", "kvm", "whpx"}
EXPECTED = ("-machine", "pc", "-smp", "1", "-m", "512M",
            "-display", "none", "-serial", "stdio", "-no-reboot")


def validate(argv):
    if not isinstance(argv, list) or not all(type(x) is str for x in argv):
        raise ValueError("argv must be an array of strings")
    if len(argv) != 18:
        raise ValueError("expected exactly 18 argv elements")
    if not argv[0] or pathlib.PurePath(argv[0].replace("\\", "/")).name not in (
        "qemu-system-x86_64", "qemu-system-x86_64.exe"
    ):
        raise ValueError("unexpected executable basename")
    if argv[1] != "-accel" or argv[2] not in ALLOWED_ACCEL:
        raise ValueError("unsupported accelerator")
    if tuple(argv[3:14]) != EXPECTED:
        raise ValueError("noncanonical or unsafe QEMU options")
    if argv[14] != "-kernel":
        raise ValueError("missing kernel option")
    kernel = argv[15]
    if not kernel or kernel.startswith("-") or "\x00" in kernel or "\n" in kernel:
        raise ValueError("invalid kernel argument")
    if argv[16:] != ["-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"]:
        raise ValueError("unexpected device or trailing option")
    return {
        "pass": True, "vcpus_requested": 1, "guest_ram_mib_requested": 512,
        "acceleration_requested": argv[2], "kernel_argument": kernel,
        "warning": "Only checks argv syntax; does not execute QEMU or prove its resource use"
    }


def main():
    if len(sys.argv) != 2:
        print("usage: validate.py argv.json", file=sys.stderr)
        return 2
    try:
        with open(sys.argv[1], encoding="utf-8") as f:
            args = json.load(f)
        print(json.dumps(validate(args), sort_keys=True))
        return 0
    except (OSError, ValueError) as exc:
        print(json.dumps({"pass": False, "error": str(exc)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
