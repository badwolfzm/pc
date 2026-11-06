#!/usr/bin/env python3
"""Opt-in QEMU runner for Warden's one-vCPU/512 MiB profile.

No command shell. Does NOT prove Doom boot, guest RSS, or game process count.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import subprocess
import sys
import threading

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHECKER = ROOT / 'tools/doom-qemu-argv-boundary/validate.py'
MAX_LOG_BYTES = 1024 * 1024


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def check_file(raw_path, digest, label):
    if not isinstance(digest, str) or re.fullmatch('[0-9a-fA-F]{64}', digest) is None:
        raise ValueError(label + ' requires a 64-character SHA-256 hex digest')
    path = pathlib.Path(raw_path).resolve(strict=True)
    if not path.is_file():
        raise ValueError(label + ' must be a regular file')
    actual = sha256_file(path)
    if actual.lower() != digest.lower():
        raise ValueError(label + ' SHA-256 mismatch')
    return str(path), actual


def check_args(argv):
    spec = importlib.util.spec_from_file_location('doom_qemu_argv', CHECKER)
    if spec is None or spec.loader is None:
        raise ValueError('merged argv validator unavailable')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.validate(argv)


def capture_command(argv, timeout_seconds, max_log_bytes=MAX_LOG_BYTES):
    """Run argv directly; bounded memory log; kill on log limit or timeout.

    Returns raw command results, not guest-success interpretation.
    """
    if type(timeout_seconds) is not int or not 1 <= timeout_seconds <= 600:
        raise ValueError('timeout must be integer seconds in [1,600]')
    if type(max_log_bytes) is not int or not 1 <= max_log_bytes <= MAX_LOG_BYTES:
        raise ValueError('invalid log limit')
    proc = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, shell=False)
    output = bytearray()
    overflowing = threading.Event()

    def reader():
        try:
            while True:
                data = proc.stdout.read(4096)
                if not data:
                    break
                room = max_log_bytes - len(output)
                if room > 0:
                    output.extend(data[:room])
                if len(data) > room:
                    overflowing.set()
                    proc.kill()
                    break
        except (OSError, ValueError):
            overflowing.set()
            try:
                proc.kill()
            except OSError:
                pass

    worker = threading.Thread(target=reader, daemon=True)
    worker.start()
    timed_out = False
    try:
        return_code = proc.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        return_code = proc.wait()
    worker.join(timeout=5)
    proc.stdout.close()
    if worker.is_alive():
        raise RuntimeError('log reader failed to terminate')
    return {'returncode': return_code, 'timed_out': timed_out,
            'log_overflow': overflowing.is_set(), 'log': bytes(output)}


def run(qemu, qemu_hash, kernel, kernel_hash, accel, timeout, log_path, report_path):
    qemu_path, actual_qemu_hash = check_file(qemu, qemu_hash, 'QEMU')
    kernel_path, actual_kernel_hash = check_file(kernel, kernel_hash, 'kernel')
    if accel not in ('tcg', 'kvm', 'whpx'):
        raise ValueError('unsupported accelerator')
    argv = [qemu_path, '-accel', accel, '-machine', 'pc', '-smp', '1', '-m', '512M',
            '-display', 'none', '-serial', 'stdio', '-no-reboot', '-kernel', kernel_path,
            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
    config = check_args(argv)
    log_path = pathlib.Path(log_path)
    report_path = pathlib.Path(report_path)
    protected = {pathlib.Path(qemu_path), pathlib.Path(kernel_path)}
    destinations = {log_path.resolve(), report_path.resolve()}
    if len(destinations) != 2 or destinations & protected:
        raise ValueError('output files must differ from each other and from input binaries')
    if any(not p.parent.is_dir() for p in (log_path, report_path)):
        raise ValueError('output parent directory does not exist')
    result = capture_command(argv, timeout)
    log_path.write_bytes(result['log'])
    report = {
        'profile': config, 'qemu_sha256': actual_qemu_hash,
        'kernel_sha256': actual_kernel_hash,
        'serial_log_sha256': hashlib.sha256(result['log']).hexdigest(),
        'serial_log_bytes': len(result['log']), 'returncode': result['returncode'],
        'timed_out': result['timed_out'], 'log_overflow': result['log_overflow'],
        'guest_boot_verified': False, 'doom_executed_verified': False,
        'note': 'raw host execution only; serial logs need independent guest evidence validation',
    }
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return 0 if result['returncode'] == 0 and not result['timed_out'] and not result['log_overflow'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('qemu', 'qemu-sha256', 'kernel', 'kernel-sha256', 'log', 'report'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--accel', choices=['tcg', 'kvm', 'whpx'], default='tcg')
    parser.add_argument('--timeout', type=int, default=120)
    a = parser.parse_args()
    try:
        return run(a.qemu, a.qemu_sha256, a.kernel, a.kernel_sha256,
                   a.accel, a.timeout, a.log, a.report)
    except (OSError, ValueError, RuntimeError) as exc:
        print('runner refused: ' + str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
