#!/usr/bin/env python3
"""Linux-only, host-side resource envelope probe; not a security sandbox."""
import argparse
import json
import os
import resource
import signal
import subprocess
import sys
import time


def positive_int(raw):
    try:
        number = int(raw)
    except ValueError as exc:
        raise argparse.ArgumentTypeError('expected an integer') from exc
    if number < 1 or number > 2**31 - 1:
        raise argparse.ArgumentTypeError('expected integer in [1, 2^31-1]')
    return number


def run(argv, memory_mib, seconds, cpu):
    if not sys.platform.startswith('linux') or not hasattr(os, 'sched_getaffinity'):
        raise RuntimeError('Linux with CPU affinity support required')
    allowed = os.sched_getaffinity(0)
    if cpu not in allowed:
        raise ValueError(f'CPU {cpu} not in allowed affinity set {sorted(allowed)}')
    memory_bytes = memory_mib * 1024 * 1024

    def setup_child():
        os.sched_setaffinity(0, {cpu})
        resource.setrlimit(resource.RLIMIT_AS, (memory_bytes, memory_bytes))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    started = time.monotonic()
    child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=True,
                             preexec_fn=setup_child, close_fds=True)
    timed_out = False
    try:
        try:
            child.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
    finally:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if child.poll() is None:
            child.wait()
    elapsed = time.monotonic() - started
    return dict(command=argv, cpu=cpu, memory_limit_mib=memory_mib,
                wall_limit_seconds=seconds, wall_seconds=round(elapsed, 6),
                exit_code=child.returncode, timed_out=timed_out,
                scope='host process address-space limit only; no FPS or guest RAM claim')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--memory-mib', type=positive_int, required=True)
    parser.add_argument('--seconds', type=positive_int, required=True)
    parser.add_argument('--cpu', type=int, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command and args.command[0] == '--' else args.command
    if not command:
        parser.error('missing command after --')
    try:
        result = run(command, args.memory_mib, args.seconds, args.cpu)
    except (OSError, RuntimeError, ValueError) as exc:
        parser.exit(2, f'error: {exc}\n')
    print(json.dumps(result, sort_keys=True))
    return 124 if result['timed_out'] else (0 if result['exit_code'] == 0 else 1)


if __name__ == '__main__':
    sys.exit(main())
