#!/usr/bin/env python3
"""Fail-closed validator for Warden swarm QEMU launch arguments.

Scope: checks launch configuration only. Does not attest guest memory usage,
GPU isolation, kernel execution, or network-fabric security.
"""
import argparse
import re
import shlex
import sys

MIB = 1024 * 1024

def memory_mib(value):
    match = re.fullmatch(r"([0-9]+)([kKmMgG]?)", value)
    if not match:
        raise ValueError("invalid QEMU -m value")
    number = int(match.group(1))
    unit = match.group(2).lower()
    bytes_ = number * {"": MIB, "k": 1024, "m": MIB, "g": 1024*MIB}[unit]
    if bytes_ <= 0 or bytes_ > 512*MIB:
        raise ValueError("guest memory must be 1..512 MiB")
    return bytes_ // MIB

def validate(args):
    """Return a checked summary; reject ambiguous/unsupported overrides."""
    if not args or not args[0].startswith("qemu-system-"):
        raise ValueError("expected qemu-system-* executable")
    values = {}
    for i, arg in enumerate(args[1:], 1):
        if arg in ("-smp", "-m"):
            if arg in values or i + 1 >= len(args):
                raise ValueError("missing or duplicate " + arg)
            values[arg] = args[i+1]
        if arg.startswith("-smp=") or arg.startswith("-m="):
            raise ValueError("use separate -smp and -m arguments")
        if arg in ("-readconfig", "-nodefaults") or arg.startswith("-readconfig="):
            raise ValueError("configuration indirection unsupported")
    if set(values) != {"-smp", "-m"}:
        raise ValueError("both -smp and -m are mandatory")
    if values["-smp"] != "1":
        raise ValueError("exactly one vCPU required")
    ram = memory_mib(values["-m"])
    # This does not assert one GPU or graphics memory accounting.
    return {"vcpus": 1, "guest_ram_mib": ram}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", help="shell-quoted QEMU command")
    ns = parser.parse_args()
    try:
        result = validate(shlex.split(ns.command))
    except ValueError as exc:
        parser.error(str(exc))
    print(f"validated launch arguments: {result}; GPU/isolation NOT verified")

if __name__ == "__main__":
    main()
