#!/usr/bin/env python3
"""Measure a REAL Pong/QEMU execution against host monotonic time.

Never trust guest-reported FPS as a wall-clock result without this comparison.
This tool starts the existing trusted Warden test script locally.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

READY = "pong: ready"
SUCCESS = "ALL HELD: 15 checks, 0 failed"
TICK_RE = re.compile(r"pong: screen hash .* (\d+) timer ticks")
REPORT_RE = re.compile(r"(?m)^report pong (40|41|42|43) (\d+)$")

def validate(log, wall, tolerance):
    if SUCCESS not in log or "PONG RUN FAILED" in log:
        raise ValueError("real guest did not pass native Warden checks")
    seen = {}
    for key, val in REPORT_RE.findall(log):
        if key in seen:
            raise ValueError("duplicate physics report")
        seen[key] = int(val)
    if set(seen) != {"40", "41", "42", "43"}:
        raise ValueError("missing physics cadence reports 40..43")
    a, b, c = seen["40"], seen["41"], seen["42"]
    if not (150 <= a <= 220 and 260 <= b <= 345 and 360 <= c <= 485):
        raise ValueError(f"physics step counts inconsistent with 60 Hz for 3/5/7 s: {a}, {b}, {c}")
    if not a < b < c:
        raise ValueError("physics counters must increase across phases")
    if not (0 <= seen["43"] <= 50):
        raise ValueError("excessive fixed-step catch-up saturation")
    match = TICK_RE.search(log)
    if not match:
        raise ValueError("missing authoritative guest tick summary")
    ticks = int(match.group(1))
    if not (695 <= ticks <= 850):
        raise ValueError(f"unexpected 100 Hz guest ticks: {ticks}")
    if not 7.2 * (1 - tolerance) <= wall <= 7.2 * (1 + tolerance):
        raise ValueError(f"wall {wall:.3f}s disagrees with expected ~7.2s by >{tolerance:.0%}")
    return {
        "scope": "QEMU Pong timer validation only; host monotonic ready-to-summary, not Albion or GPU FPS",
        "host_monotonic_ready_to_summary_seconds": round(wall, 4),
        "guest_kernel_ticks_at_end": ticks,
        "physics_steps_at_3_seconds": a,
        "physics_steps_at_5_seconds": b,
        "physics_steps_at_7_seconds": c,
        "saturated_catchup_events": seen["43"],
        "host_wall_tolerance_fraction": tolerance,
        "clock_contract_accepted": True,
    }

def monitor(root, log_path, timeout):
    env = dict(os.environ)
    env["LOG"] = str(log_path)
    env["SHOW"] = "0"
    output_path = log_path.with_suffix(".runner.txt")
    started = done = None
    with output_path.open("w") as output:
        proc = subprocess.Popen(["bash", "tests/kernel/pong.sh"], cwd=root, env=env,
                                stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                now = time.monotonic()
                log = log_path.read_text(errors="replace") if log_path.exists() else ""
                if started is None and READY in log:
                    started = now
                if done is None and SUCCESS in log:
                    done = now
                if proc.poll() is not None and done is not None:
                    break
                if proc.poll() is not None and done is None:
                    raise ValueError("QEMU exited without kernel success")
                time.sleep(.01)
            else:
                raise ValueError("timed out watching QEMU")
            if proc.wait(timeout=5) != 0:
                raise ValueError("Pong runner returned nonzero status")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
    if started is None or done is None or done <= started:
        raise ValueError("could not independently observe guest ready and success")
    return log_path.read_text(errors="replace"), done - started

def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--log", type=Path, default=Path("target/pong-clock-serial.log"))
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--wall-tolerance", type=float, default=.40)
    args = parser.parse_args(argv)
    if not 0.05 <= args.wall_tolerance <= .75:
        parser.error("wall tolerance must be between 0.05 and 0.75")
    root = args.root.resolve()
    log = (root / args.log).resolve()
    if not log.is_relative_to(root):
        parser.error("log must be inside working tree")
    log.parent.mkdir(parents=True, exist_ok=True)
    try:
        text, elapsed = monitor(root, log, args.timeout)
        report = validate(text, elapsed, args.wall_tolerance)
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f"CLOCK NOT VERIFIED: {exc}", file=sys.stderr)
        print(f"Review {log} and {log.with_suffix('.runner.txt')}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    print("CLOCK VERIFIED against host monotonic wall time (within specified tolerance)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
