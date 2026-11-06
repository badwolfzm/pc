#!/usr/bin/env python3
"""Independent two-boot evidence comparison for real native Warden Pong."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'ordinary512-native-qemu'))
from evidence import extract, Invalid

MAX_LOG_BYTES = 2 * 1024 * 1024

def combine(log_a, log_b, manifest):
    if not isinstance(log_a, str) or not isinstance(log_b, str):
        raise ValueError('two text logs required')
    if not log_a or not log_b or len(log_a.encode('utf-8')) > MAX_LOG_BYTES or len(log_b.encode('utf-8')) > MAX_LOG_BYTES:
        raise ValueError('empty or oversized serial log')
    if log_a == log_b:
        raise ValueError('byte-identical logs do not provide independent-run evidence')
    one = extract(log_a, manifest)
    two = extract(log_b, manifest)
    if one['screen_hash'] != two['screen_hash']:
        raise ValueError('framebuffer screen hashes disagree between runs')
    if one['kernel_checks_passed'] != 15 or two['kernel_checks_passed'] != 15:
        raise ValueError('missing kernel isolation check evidence')
    return {
        'profile': 'native-pong-qemu-512m-smp1-two-boot',
        'guest_ram_configured_bytes': 512 * 1024 * 1024,
        'guest_vcpus_configured': 1,
        'distinct_serial_sha256': [
            hashlib.sha256(log_a.encode('utf-8')).hexdigest(),
            hashlib.sha256(log_b.encode('utf-8')).hexdigest()
        ],
        'matched_screen_hash': one['screen_hash'],
        'runs': [one, two],
        'scope': 'two separately collected native Pong QEMU serial logs; not full physical guest RAM accounting, 300-frame benchmark, Doom, or modern PC game proof'
    }

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('first_log', type=Path)
    parser.add_argument('second_log', type=Path)
    parser.add_argument('profile_manifest', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args(argv)
    try:
        logs = []
        for path in (args.first_log, args.second_log):
            if not path.is_file() or path.stat().st_size > MAX_LOG_BYTES:
                raise ValueError('log missing or exceeds 2 MiB')
            logs.append(path.read_text(encoding='utf-8'))
        if args.profile_manifest.stat().st_size > 8192:
            raise ValueError('oversized manifest')
        manifest = json.loads(args.profile_manifest.read_text(encoding='utf-8'))
        report = combine(*logs, manifest)
        output = json.dumps(report, indent=2, sort_keys=True) + '\n'
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output, encoding='utf-8')
        else:
            print(output, end='')
        return 0
    except (ValueError, OSError, UnicodeError, Invalid) as exc:
        print('TWO-BOOT EVIDENCE REJECTED:', exc, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
