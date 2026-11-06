#!/usr/bin/env python3
"""Observe actual QEMU HOST memory during native Warden Pong. Not guest RAM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

def parse_status(s):
    found = {}
    for line in s.splitlines():
        if line.startswith(('VmRSS:', 'VmHWM:', 'Threads:')):
            k, v = line.split(':', 1)
            parts = v.split()
            if k == 'Threads' and len(parts) == 1:
                found[k] = int(parts[0])
            elif k != 'Threads' and len(parts) == 2 and parts[1] == 'kB':
                found[k] = int(parts[0]) * 1024
            else:
                raise ValueError('invalid /proc status')
    return found

def verify_qemu(argv):
    def opt(key):
        hits = [i for i, v in enumerate(argv) if v == key]
        if len(hits) != 1 or hits[0] + 1 >= len(argv):
            raise ValueError('missing or duplicate '+key)
        return argv[hits[0]+1]
    if opt('-m') != '512M' or opt('-smp') != '1':
        raise ValueError('not one-vCPU/512MiB QEMU')
    return True

def descendants(pid):
    found, stack = set(), [pid]
    while stack and len(found) < 128:
        parent = stack.pop()
        try:
            raw = (Path('/proc') / str(parent) / 'task' / str(parent) / 'children').read_text()
        except (OSError, ValueError):
            continue
        for item in raw.split():
            p = int(item)
            if p not in found:
                found.add(p)
                stack.append(p)
    return found

def sample(script, manifest, log, report, interval_ms=20, timeout_sec=220):
    if not (5 <= interval_ms <= 1000 and 1 <= timeout_sec <= 240):
        raise ValueError('invalid interval or timeout')
    m = json.loads(manifest.read_text())
    if (m.get('qemu_option') != '-m 512M -smp 1' or
        type(m.get('guest_ram_bytes')) is not int or m['guest_ram_bytes'] != 512*1024*1024 or
        type(m.get('guest_vcpus')) is not int or m['guest_vcpus'] != 1):
        raise ValueError('bad Warden profile manifest')
    if script.name != 'pong.sh' or script.resolve().parent.name != 'kernel':
        raise ValueError('refuse non-Pong script')
    txt = script.read_text()
    if txt.count('-m 512M -smp 1') != 1 or txt.count('qemu-system-x86_64') != 1:
        raise ValueError('unexpected QEMU script configuration')
    report.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['LOG'] = str(log)
    env['SCREENSHOT'] = str(report.parent / 'qemu-rss-screen.ppm')
    child = subprocess.Popen(['bash', str(script)], env=env, start_new_session=True)
    t0 = time.monotonic()
    pid = None
    samples = 0
    peak_rss = peak_hwm = peak_threads = 0
    cmd_sha = None
    timed_out = False
    try:
        while child.poll() is None:
            if time.monotonic() - t0 > timeout_sec:
                timed_out = True
                break
            for candidate in descendants(child.pid):
                try:
                    raw = (Path('/proc')/str(candidate)/'cmdline').read_bytes()
                    argv = [p.decode('utf-8', errors='replace') for p in raw.split(b'\x00') if p]
                    if not argv or Path(argv[0]).name != 'qemu-system-x86_64':
                        continue
                    verify_qemu(argv)
                    if pid is not None and pid != candidate:
                        raise ValueError('multiple QEMU PIDs observed')
                    pid = candidate
                    cmd_sha = hashlib.sha256(raw).hexdigest()
                    status = parse_status((Path('/proc')/str(candidate)/'status').read_text())
                    if status.get('VmRSS',0) > 0 and status.get('Threads',0) > 0:
                        samples += 1
                        peak_rss = max(peak_rss, status['VmRSS'])
                        peak_hwm = max(peak_hwm, status.get('VmHWM',0))
                        peak_threads = max(peak_threads, status['Threads'])
                except FileNotFoundError:
                    continue
            time.sleep(interval_ms/1000)
    finally:
        if child.poll() is None:
            try:
                os.killpg(child.pid,signal.SIGKILL)
            except ProcessLookupError:
                pass
        child.wait()
    result = {
        'evidence_kind':'actual-native-warden-qemu-host-process',
        'guest_ram_option_mib':512, 'guest_vcpus_option':1,
        'qemu_observed':pid is not None, 'qemu_cmdline_sha256':cmd_sha,
        'qemu_host_peak_sampled_rss_bytes':peak_rss,
        'qemu_host_peak_reported_hwm_bytes':peak_hwm,
        'qemu_host_peak_thread_count':peak_threads,
        'samples':samples,'child_exit_code':child.returncode,'timed_out':timed_out,
        'elapsed_seconds':round(time.monotonic()-t0,3),
        'scope':'QEMU HOST process RSS/HWM, not guest physical RAM, page faults, swap or game performance'
    }
    report.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    if timed_out or samples < 2 or child.returncode != 0:
        raise ValueError('real Warden/QEMU run did not pass or lacked sampling')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--log',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args()
    try:
        print(json.dumps(sample(Path('tests/kernel/pong.sh'),a.manifest,a.log,a.report),sort_keys=True))
    except (OSError,ValueError) as e:
        print('QEMU HOST RSS CHECK FAILED:',e,file=sys.stderr)
        return 1
    return 0

if __name__=='__main__':
    sys.exit(main())
