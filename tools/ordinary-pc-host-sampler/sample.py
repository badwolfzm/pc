#!/usr/bin/env python3
"""Measure real Linux host process RSS/faults on one CPU. NOT a Warden guest cap."""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def parse_stat(content):
    """Linux /proc/PID/stat; comm may contain spaces and parentheses."""
    end = content.rfind(")")
    if end < 0:
        raise ValueError("invalid process stat")
    parts = content[end + 1:].split()
    if len(parts) < 22:
        raise ValueError("incomplete process stat")
    return {
        "minor_faults": int(parts[7]),
        "major_faults": int(parts[9]),
        "cpu_ticks": int(parts[11]) + int(parts[12]),
        "thread_count": int(parts[17]),
        "rss_pages": int(parts[21]),
    }


def parse_status(content):
    result = {}
    for line in content.splitlines():
        if line.startswith(("VmRSS:", "VmHWM:")):
            name, value = line.split(":", 1)
            fields = value.split()
            if len(fields) != 2 or fields[1] != "kB":
                raise ValueError("unexpected status memory units")
            result[name] = int(fields[0]) * 1024
    return result


def read_process(pid):
    root = Path("/proc") / str(pid)
    stat = parse_stat((root / "stat").read_text())
    status = parse_status((root / "status").read_text())
    return {
        **stat,
        "rss_bytes": status.get("VmRSS", stat["rss_pages"] * os.sysconf("SC_PAGE_SIZE")),
        "hwm_bytes": status.get("VmHWM"),
    }


def sample(command, cpu, budget_mib=512, period_ms=20, timeout_sec=10):
    if not command or not isinstance(cpu, int) or cpu < 0:
        raise ValueError("command and nonnegative CPU index required")
    if budget_mib <= 0 or period_ms < 1 or timeout_sec <= 0:
        raise ValueError("budget, interval and timeout must be positive")
    if not hasattr(os, "sched_setaffinity"):
        raise RuntimeError("Linux CPU affinity required")
    if cpu not in os.sched_getaffinity(0):
        raise ValueError(f"CPU {cpu} not allowed by current host affinity")
    # Force the child onto a single allowed CPU *before exec*, so there is no
    # initial multi-CPU run interval. preexec_fn is only safe in a single-threaded
    # launcher; this CLI does not start any threads.
    def pin_cpu():
        os.sched_setaffinity(0, {cpu})

    p = subprocess.Popen(command, preexec_fn=pin_cpu, start_new_session=True)
    start = time.monotonic()
    last = None
    first = None
    samples = 0
    max_rss = 0
    max_hwm = 0
    max_threads = 0
    timed_out = False
    try:
        while True:
            now = time.monotonic()
            try:
                point = read_process(p.pid)
                if first is None:
                    first = point
                last = point
                samples += 1
                max_rss = max(max_rss, point["rss_bytes"])
                max_hwm = max(max_hwm, point["hwm_bytes"] or 0)
                max_threads = max(max_threads, point["thread_count"])
            except (FileNotFoundError, ProcessLookupError):
                pass  # child exited between reads
            if p.poll() is not None:
                break
            if now - start >= timeout_sec:
                timed_out = True
                # Kill the complete process group, not only its group leader.
                os.killpg(p.pid, 9)
                break
            time.sleep(period_ms / 1000.0)
    finally:
        if p.poll() is None:
            os.killpg(p.pid, 9)
        code = p.wait()
    elapsed = time.monotonic() - start
    return {
        "scope": "Linux host process only; not Warden guest total physical RAM",
        "command": command,
        "cpu_affinity": [cpu],
        "budget_mib_observational_not_enforced": budget_mib,
        "pid": p.pid,
        "exit_code": code,
        "timeout": timed_out,
        "elapsed_seconds": round(elapsed, 6),
        "sample_count": samples,
        "peak_sampled_rss_bytes": max_rss,
        "peak_reported_vmhwm_bytes": max_hwm if max_hwm else None,
        "peak_threads_observed": max_threads,
        "minor_faults_delta": (last["minor_faults"] - first["minor_faults"]) if first and last else None,
        "major_faults_delta": (last["major_faults"] - first["major_faults"]) if first and last else None,
        "rss_over_budget_observed": max(max_rss, max_hwm) > budget_mib * 1024 * 1024,
        "limitations": [
            "Host RSS is not total guest RAM and is not a memory limit",
            "Child descendants and GPU allocations are not included",
            "Sampling misses events shorter than the polling interval",
            "Fault deltas exclude faults before the first sample and after the last",
            "One-core affinity applies to the launched process and inherited affinity, not external helpers",
            "Host swap/cache or game compatibility cannot be inferred from these counters"
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu", required=True, type=int, help="allowed Linux logical CPU ID")
    parser.add_argument("--budget-mib", type=int, default=512)
    parser.add_argument("--period-ms", type=int, default=20)
    parser.add_argument("--timeout-sec", type=float, default=10)
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- /path/to/authorized/workload [args...]")
    args = parser.parse_args()
    cmd = args.command[1:] if args.command and args.command[0] == "--" else args.command
    try:
        result = sample(cmd, args.cpu, args.budget_mib, args.period_ms, args.timeout_sec)
    except (ValueError, OSError, RuntimeError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["exit_code"] == 0 and not result["timeout"] and result["sample_count"] > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
