#!/usr/bin/env python3
"""Strict ET_REL join of genuine Doom runtime with existing guest entry and FILE provider.

This does not create a bootable ELF, supply a WAD grant, or run Warden.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

FILE_APIS = frozenset({'fopen', 'fclose', 'fread', 'fseek', 'ftell'})
EXPECTED = FILE_APIS | {'exit', 'printf', 'puts', 'system'}
PROTECTED = frozenset({'fwrite', 'remove', 'rename'})
REQUIRED_ARENA = 8 * 1024 * 1024
REQUIRED_ZONE = 6 * 1024 * 1024


def symbols(text):
    defs, undefs = set(), set()
    for line in text.splitlines():
        fields = line.split()
        if not fields:
            continue
        if len(fields) < 2:
            raise ValueError('malformed nm -P entry')
        name, typ = fields[:2]
        if typ == 'U':
            undefs.add(name)
        elif typ.upper() in {'T', 'D', 'B', 'R', 'W', 'V', 'C', 'S', 'G'}:
            defs.add(name)
        else:
            raise ValueError('unexpected nm symbol type: ' + typ)
    return defs, undefs


def verify(before, after, existing_defs, extras):
    if not EXPECTED <= before:
        raise ValueError('baseline lacks the real unresolved APIs: ' + repr(sorted(EXPECTED - before)))
    if not PROTECTED <= before:
        raise ValueError('baseline does not expose protected imports')
    seen = set(existing_defs)
    for name, definitions, _ in extras:
        overlap = seen & definitions
        if overlap:
            raise ValueError('duplicate symbol in ' + name + ': ' + repr(sorted(overlap)))
        seen.update(definitions)
    if '_start' not in seen:
        raise ValueError('missing genuine Warden startup entry')
    if after != before - EXPECTED:
        raise ValueError('unexpected import closure: added=' + repr(sorted(after - before)) +
                         ' missing=' + repr(sorted((before - EXPECTED) - after)) +
                         ' not_closed=' + repr(sorted(after & EXPECTED)))
    if not PROTECTED <= after:
        raise ValueError('host access was silently provided')
    return sorted(EXPECTED)



def verify_runtime_report(report, runtime_digest, scanner_report=None):
    if not isinstance(report, dict):
        raise ValueError('invalid runtime report type')
    if report.get('arena_bytes_reserved_in_BSS') != REQUIRED_ARENA:
        raise ValueError('runtime arena must be exactly the reviewed 8 MiB')
    if report.get('pinned_default_doom_zone_bytes') != REQUIRED_ZONE:
        raise ValueError('6 MiB genuine Doom startup-zone evidence missing')
    base_digest = report.get('candidate_object_sha256')
    if not isinstance(base_digest, str) or len(base_digest) != 64:
        raise ValueError('missing original runtime SHA256')
    if scanner_report is None:
        if base_digest != runtime_digest:
            raise ValueError('runtime report does not match actual linked object')
    else:
        if not isinstance(scanner_report, dict):
            raise ValueError('invalid scanner integration report')
        if scanner_report.get('original_bundle_object_sha256') != base_digest:
            raise ValueError('scanner input not bound to 8 MiB runtime report')
        if scanner_report.get('combined_object_sha256') != runtime_digest:
            raise ValueError('scanner report not bound to actual 17-provider object')
        if scanner_report.get('resolved_only') != ['__isoc99_sscanf']:
            raise ValueError('scanner may close only one genuine imported symbol')
        if scanner_report.get('scanner_exact_commit') != '62dc9f73bee7753bd81d889a0e44ee8e95ce2220':
            raise ValueError('scanner must come from pinned reviewed candidate')
        if scanner_report.get('real_engine_upstream_commit') != report.get('upstream_commit'):
            raise ValueError('scanner upstream provenance mismatch')
        if scanner_report.get('final_executable_built') is not False:
            raise ValueError('scanner report misrepresents ELF execution status')
    metrics = report.get('static_elf_allocatable_section_metrics')
    if not isinstance(metrics, dict) or metrics.get('allocatable_nobits_bytes', 0) < REQUIRED_ARENA:
        raise ValueError('8 MiB NOBITS reservation not demonstrated in actual ELF')
    if metrics.get('allocatable_total_section_bytes', REQUIRED_ARENA * 1024) >= 512 * 1024 * 1024:
        raise ValueError('linked static sections exceed 512 MiB guest budget')
    return True


def call(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError('command failed: ' + repr(cmd) + '\n' + result.stderr[-2500:])
    return result.stdout



def final_link_probe(combined, missing, executable):
    """Attempt actual ET_EXEC link with native _start, never fabricate providers."""
    if not missing:
        raise ValueError('negative final-link gate requires unresolved genuine imports')
    if executable.exists():
        raise ValueError('refuse to overwrite an existing executable')
    args = ['ld', '-nostdlib', '-static', '--no-undefined', '-e', '_start',
            '-Ttext', '0x800000', '-o', str(executable), str(combined)]
    result = subprocess.run(args, text=True, capture_output=True, timeout=60,
                            check=False)
    if result.returncode == 0 or executable.exists():
        raise ValueError('unexpected executable/link success: independent review required')
    stderr = result.stderr[:16384]
    if 'undefined reference' not in stderr:
        raise ValueError('final linker failed without genuine missing-symbol evidence')
    return {
        'entry': '_start',
        'attempted': True,
        'linker_exit_code': result.returncode,
        'executable_built': False,
        'remaining_imports': sorted(missing),
        'stderr_excerpt': stderr,
        'address_assumption_not_loader_approved': '0x800000',
    }


def run(runtime, runtime_report, output, entry, exit_source, file_source, scanner_report=None,
        console_source=None, no_shell_source=None):
    if console_source is None or no_shell_source is None:
        raise ValueError('missing canonical merged console and no-shell providers')
    for file in (runtime, entry, exit_source, file_source, console_source, no_shell_source):
        if not file.is_file():
            raise ValueError('missing required input: ' + str(file))
    if not runtime_report.is_file():
        raise ValueError('missing canonical runtime report')
    if scanner_report is not None and not scanner_report.is_file():
        raise ValueError('missing scanner integration report')
    scanner_data = json.loads(scanner_report.read_text()) if scanner_report else None
    verify_runtime_report(json.loads(runtime_report.read_text()),
                          hashlib.sha256(runtime.read_bytes()).hexdigest(), scanner_data)
    output.mkdir(parents=True, exist_ok=True)
    def inspect(obj):
        return symbols(call(['nm', '-P', '-g', str(obj)]))
    base_defs, before = inspect(runtime)
    extras = []
    objects = []
    flags = ['-std=c11', '-O2', '-ffreestanding', '-fno-builtin',
             '-fno-stack-protector', '-fno-pie', '-fno-pic']
    for name, source in [('entry', entry), ('exit', exit_source), ('file', file_source),
                         ('console', console_source), ('no_shell', no_shell_source)]:
        obj = output / (name + '.o')
        call(['cc', *flags, '-c', str(source), '-o', str(obj)])
        defs, missing = inspect(obj)
        extras.append((name, defs, missing))
        objects.append(obj)
    combined = output / 'doom-entry-file-candidate.o'
    call(['ld', '-r', '-o', str(combined), str(runtime), *(str(p) for p in objects)])
    combined_defs, after = inspect(combined)
    closed = verify(before, after, base_defs, extras)
    final_probe = final_link_probe(combined, after, output / 'doom-native-candidate.elf')
    if '_start' not in combined_defs:
        raise ValueError('entry symbol lost in partial link')
    result = {
        'kind': 'ET_REL diagnostic; NOT executable or Warden guest execution',
        'input_sha256': {name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for name, p in [('runtime', runtime), ('entry', entry),
                                         ('exit', exit_source), ('file', file_source),
                                         ('console', console_source), ('no_shell', no_shell_source)]},
        'combined_sha256': hashlib.sha256(combined.read_bytes()).hexdigest(),
        'final_link_probe': final_probe,
        'removed_imports': closed,
        'remaining_imports': sorted(after),
        'guest_boot_verified': False,
        'wad_capability_installed': False,
        'memory_measured': False,
        'frames_measured': False,
    }
    (output / 'report.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime-object', required=True, type=Path)
    p.add_argument('--runtime-report', required=True, type=Path)
    p.add_argument('--scanner-report', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--entry', default='tools/doom-native-entry/entry.c', type=Path)
    p.add_argument('--exit-source', default='tools/doom-native-safe-exit/exit.c', type=Path)
    p.add_argument('--console-source', default='tools/doom-native-console/console.c', type=Path)
    p.add_argument('--no-shell-source', default='tools/doom-native-no-shell/no_shell.c', type=Path)
    p.add_argument('--file-source', required=True, type=Path,
                   help='exact source from reviewed PR #264; not a host FILE stub')
    args = p.parse_args()
    try:
        print(json.dumps(run(args.runtime_object, args.runtime_report, args.output, args.entry,
                             args.exit_source, args.file_source, args.scanner_report,
                             args.console_source, args.no_shell_source), sort_keys=True))
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        p.error(str(exc))
