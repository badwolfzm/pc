#!/usr/bin/env python3
"""Two native Pong QEMU host-memory reports; do not equate RSS with guest RAM."""
import argparse
import json
import math
from pathlib import Path
import sys

PROFILE = ('actual-native-warden-qemu-host-process', 512, 1)

def compare(a, b):
    for n, r in enumerate((a, b), 1):
        if not isinstance(r, dict):
            raise ValueError(f'boot {n}: not a report')
        kind, mib, cpu = PROFILE
        if r.get('evidence_kind') != kind or type(r.get('guest_ram_option_mib')) is not int or r['guest_ram_option_mib'] != mib or type(r.get('guest_vcpus_option')) is not int or r['guest_vcpus_option'] != cpu:
            raise ValueError(f'boot {n}: wrong QEMU profile')
        if r.get('qemu_observed') is not True or r.get('timed_out') is not False or type(r.get('child_exit_code')) is not int or r['child_exit_code'] != 0:
            raise ValueError(f'boot {n}: process failure or no QEMU')
        for field in ('samples','qemu_host_peak_sampled_rss_bytes','qemu_host_peak_reported_hwm_bytes','qemu_host_peak_thread_count'):
            if type(r.get(field)) is not int or r[field] <= 0:
                raise ValueError(f'boot {n}: missing {field}')
        if r['samples'] < 2 or r['qemu_host_peak_reported_hwm_bytes'] < r['qemu_host_peak_sampled_rss_bytes']:
            raise ValueError(f'boot {n}: inconsistent RSS observations')
        for field in ('qemu_host_minor_fault_delta_observed',
                      'qemu_host_major_fault_delta_observed',
                      'qemu_host_cpu_ticks_delta_observed'):
            if type(r.get(field)) is not int or r[field] < 0:
                raise ValueError(f'boot {n}: invalid host counter {field}')
        for field in ('qemu_observed_span_seconds',
                      'launcher_elapsed_seconds_including_build'):
            value = r.get(field)
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise ValueError(f'boot {n}: invalid duration {field}')
        if r['qemu_observed_span_seconds'] > r['launcher_elapsed_seconds_including_build']:
            raise ValueError(f'boot {n}: QEMU span exceeds launcher duration')
        sha = r.get('qemu_cmdline_sha256')
        if not isinstance(sha, str) or len(sha) != 64 or any(c not in '0123456789abcdef' for c in sha):
            raise ValueError(f'boot {n}: no command hash')
    rss = [a['qemu_host_peak_sampled_rss_bytes'], b['qemu_host_peak_sampled_rss_bytes']]
    return {
        'profile':'native-warden-pong-qemu-host-rss-two-boots',
        'guest_ram_configured_mib':512,
        'guest_vcpus_configured':1,
        'both_qemu_runs_exited_successfully':True,
        'host_peak_sampled_rss_bytes_per_run':rss,
        'host_peak_reported_hwm_bytes_per_run':[a['qemu_host_peak_reported_hwm_bytes'],b['qemu_host_peak_reported_hwm_bytes']],
        'max_host_peak_sampled_rss_bytes':max(rss),
        'min_host_peak_sampled_rss_bytes':min(rss),
        'host_peak_rss_spread_bytes':max(rss)-min(rss),
        'host_threads_peak_per_run':[a['qemu_host_peak_thread_count'],b['qemu_host_peak_thread_count']],
        'host_minor_fault_deltas_per_run':[a['qemu_host_minor_fault_delta_observed'],b['qemu_host_minor_fault_delta_observed']],
        'host_major_fault_deltas_per_run':[a['qemu_host_major_fault_delta_observed'],b['qemu_host_major_fault_delta_observed']],
        'host_cpu_tick_deltas_per_run':[a['qemu_host_cpu_ticks_delta_observed'],b['qemu_host_cpu_ticks_delta_observed']],
        'qemu_observed_spans_seconds':[a['qemu_observed_span_seconds'],b['qemu_observed_span_seconds']],
        'launcher_elapsed_including_build_seconds':[a['launcher_elapsed_seconds_including_build'],b['launcher_elapsed_seconds_including_build']],
        'observations':[a,b],
        'scope':'Two real Pong QEMU host process samples; these host measurements do not equal guest physical residency, nor verify Doom, modern games, swap, total host process tree or full game FPS'
    }

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('first',type=Path)
    p.add_argument('second',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(argv)
    try:
        if args.first.resolve()==args.second.resolve():
            raise ValueError('same file cannot establish separate runs')
        docs=[]
        for name in (args.first,args.second):
            if not name.is_file() or name.stat().st_size > 65536:
                raise ValueError('missing or oversized report')
            docs.append(json.loads(name.read_text()))
        report=compare(*docs)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
        print(json.dumps(report,sort_keys=True))
    except (OSError,ValueError) as e:
        print('HOST RSS TWO-BOOT CHECK FAILED:',e,file=sys.stderr)
        return 1
    return 0

if __name__=='__main__':
    sys.exit(main())
