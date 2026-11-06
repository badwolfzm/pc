#!/usr/bin/env python3
"""Conservative QEMU argument lint; not a security sandbox or GPU attestation."""
import argparse
import re
import shlex
from pathlib import Path

MIB = 1024 * 1024
VALUE_FLAGS = frozenset({'-smp', '-m', '-kernel', '-append', '-accel', '-machine', '-display', '-serial'})
SWITCH_FLAGS = frozenset({'-nographic', '-no-reboot', '-no-shutdown'})

def memory_mib(value):
    match = re.fullmatch(r'([0-9]+)([kKmMgG]?)', value)
    if not match:
        raise ValueError('invalid QEMU -m value')
    number = int(match.group(1))
    unit = match.group(2).lower()
    size = number * {'': MIB, 'k': 1024, 'm': MIB, 'g': 1024 * MIB}[unit]
    if size <= 0 or size > 512 * MIB:
        raise ValueError('guest memory must be 1..512 MiB')
    if size % MIB:
        raise ValueError('memory must be an integral number of MiB')
    return size // MIB

def validate(args):
    if not args or Path(args[0]).name not in ('qemu-system-x86_64', 'qemu-system-x86_64.exe'):
        raise ValueError('expected x86-64 QEMU executable')
    values = {}
    switches = set()
    i = 1
    while i < len(args):
        flag = args[i]
        if flag in SWITCH_FLAGS:
            if flag in switches:
                raise ValueError('duplicate flag: ' + flag)
            switches.add(flag)
            i += 1
            continue
        if flag not in VALUE_FLAGS:
            raise ValueError('unsupported QEMU option: ' + flag)
        if flag in values or i + 1 >= len(args) or args[i + 1].startswith('-'):
            raise ValueError('duplicate or missing value: ' + flag)
        values[flag] = args[i + 1]
        i += 2
    if values.get('-smp') != '1':
        raise ValueError('exactly one vCPU required')
    if '-m' not in values:
        raise ValueError('explicit -m required')
    ram = memory_mib(values['-m'])
    if '-machine' in values and not re.fullmatch(r'(pc|q35)(,accel=(tcg|kvm|whpx))?', values['-machine']):
        raise ValueError('unsupported machine or memory-backend override')
    if '-accel' in values and values['-accel'] not in ('tcg', 'kvm', 'whpx'):
        raise ValueError('unsupported accelerator')
    if '-display' in values and values['-display'] not in ('none', 'sdl', 'gtk'):
        raise ValueError('unsupported display backend')
    if '-serial' in values and values['-serial'] not in ('stdio', 'none', 'mon:stdio'):
        raise ValueError('unsupported serial backend')
    return {'vcpus': 1, 'guest_ram_mib': ram}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', help='shell-quoted QEMU command; do not execute untrusted input')
    ns = parser.parse_args()
    try:
        result = validate(shlex.split(ns.command))
    except ValueError as exc:
        parser.error(str(exc))
    print(f'validated launch arguments: {result}; GPU/isolation NOT verified')

if __name__ == '__main__':
    main()
