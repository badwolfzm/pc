#!/usr/bin/env python3
"""Fail-closed proof that the existing Pong guest really executed in Warden/QEMU.

The kernel-generated log is necessary evidence, not a portable FPS benchmark.
"""
import argparse
from pathlib import Path
import re
import sys

REQUIRED = (
    "pong: ready",
    "PASS pong presented frames",
    "PASS pong completed all three timed phases",
    "PASS the screen holds exactly the frame Pong last presented (hash match)",
    "PASS no hostile thread ever put a pixel on the screen",
    "PASS hostile writer: 20000 presents refused, none slipped through",
    "PASS hostile forger: cannot mint or name a display capability from any slot",
)
SUMMARY = re.compile(r"^ALL HELD: (?P<checks>[1-9][0-9]*) checks, 0 failed$", re.MULTILINE)
PRESENTS = re.compile(r"^pong: (?P<accepted>[1-9][0-9]*) tile presents accepted", re.MULTILINE)

def validate(log: str) -> list[str]:
    errors = [f"missing: {line}" for line in REQUIRED if line not in log]
    match = SUMMARY.search(log)
    if match is None:
        errors.append("missing exact zero-failure kernel summary")
    match = PRESENTS.search(log)
    if match is None:
        errors.append("no positive framebuffer present count")
    if "PONG RUN FAILED" in log or re.search(r"^FAIL ", log, re.MULTILINE):
        errors.append("kernel reported a failed guest check")
    return errors

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("serial_log", type=Path)
    args = p.parse_args()
    try:
        log = args.serial_log.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        print(error, file=sys.stderr)
        return 2
    problems = validate(log)
    if problems:
        for error in problems:
            print("PONG GUEST NOT VERIFIED:", error, file=sys.stderr)
        return 1
    print("PONG GUEST VERIFIED: Warden/QEMU framebuffer, game loop, isolation and kernel checks")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
