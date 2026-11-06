#!/usr/bin/env python3
"""Conservative QEMU argv preflight; checks configuration only, not execution."""
import argparse
import json
import re
from pathlib import Path

class ConfigError(ValueError):
    pass

NO_ARG = {'-nographic', '-no-reboot', '-no-shutdown', '-nodefaults'}
WITH_ARG = {'-kernel', '-initrd', '-append', '-serial', '-monitor', '-display',
            '-vga', '-boot', '-accel', '-cpu', '-bios', '-drive', '-chardev',
            '-qmp', '-name', '-rtc', '-d', '-D'}
# Reject opaque QEMU expansion mechanisms.
BLOCKED = {'-machine', '-object', '-numa', '-device', '-mem-path', '-mem-prealloc',
           '-overcommit', '-incoming', '-readconfig', '-set', '-global',
           '-add-fd', '-fw_cfg', '-S', '-enable-kvm', '-smp=', '-m='}

def memory_mib(value):
    match = re.fullmatch(r'([0-9]+)([KkMmGg]?)', value)
    if not match:
        raise ConfigError('memory value must be a plain integer in KiB/MiB/GiB')
    amount, unit = int(match[1]), match[2].upper()
    if unit == 'K':
        valid = amount == 524288
    elif unit == 'G':
        valid = False
    else:
        valid = amount == 512
    if not valid:
        raise ConfigError('memory must be exactly 512 MiB')
    return 512

def cpu_count(value):
    fields = value.split(',')
    if not re.fullmatch(r'[0-9]+', fields[0]) or int(fields[0]) != 1:
        raise ConfigError('one vCPU required')
    seen = set()
    allowed = {'sockets', 'dies', 'clusters', 'cores', 'threads', 'maxcpus'}
    for item in fields[1:]:
        if item.count('=') != 1:
            raise ConfigError('invalid vCPU topology')
        key, number = item.split('=')
        if key not in allowed or key in seen or not re.fullmatch(r'[0-9]+', number) or int(number) != 1:
            raise ConfigError('unknown or non-one topology')
        seen.add(key)
    return 1

def verify(argv):
    if not isinstance(argv, list) or not 1 <= len(argv) <= 256:
        raise ConfigError('expected bounded argv JSON array')
    if any(not isinstance(s, str) or not s or len(s) > 8192 or '\x00' in s for s in argv):
        raise ConfigError('invalid argv item')
    if not re.fullmatch(r'(?:.*/)?qemu-system-x86_64(?:\.exe)?', argv[0]):
        raise ConfigError('expected qemu-system-x86_64')
    found = {}
    index = 1
    while index < len(argv):
        opt = argv[index]
        if opt in BLOCKED or opt.startswith(('-m=', '-smp=')):
            raise ConfigError('unsupported expansion or memory option: ' + opt)
        if opt in NO_ARG:
            index += 1
            continue
        if opt not in WITH_ARG | {'-m', '-smp'}:
            raise ConfigError('unknown option: ' + opt)
        if index + 1 == len(argv):
            raise ConfigError('missing value: ' + opt)
        value = argv[index + 1]
        if opt in ('-m', '-smp'):
            if opt in found:
                raise ConfigError('duplicate resource option: ' + opt)
            found[opt] = memory_mib(value) if opt == '-m' else cpu_count(value)
        index += 2
    if found != {'-m': 512, '-smp': 1}:
        raise ConfigError('both -smp 1 and -m 512M must be explicit')
    return {'configured_guest_ram_mib': 512, 'configured_vcpus': 1,
            'guest_ram_measured': False, 'native_doom_boot_verified': False,
            'host_cpu_affinity_verified': False, 'gpu_shared_memory_verified': False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('argv_json', type=Path)
    args = parser.parse_args()
    try:
        if args.argv_json.stat().st_size > 65536:
            raise ConfigError('argv JSON exceeds size limit')
        result = verify(json.loads(args.argv_json.read_text(encoding='utf-8')))
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, sort_keys=True))

if __name__ == '__main__':
    main()
