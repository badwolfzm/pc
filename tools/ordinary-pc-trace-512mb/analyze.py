#!/usr/bin/env python3
"""Bounded offline page-reference analysis for Warden's 512 MiB research profile."""
import argparse
from collections import OrderedDict, deque
import json
from pathlib import Path
import sys

PAGE_BYTES = 4096
MAX_EVENTS = 2_000_000
WINDOWS_US = (16_000, 100_000, 1_000_000)

def analyze(lines, *, reserve_mib=64, ram_mib=512, max_events=MAX_EVENTS):
    if not isinstance(reserve_mib, int) or isinstance(reserve_mib, bool) or reserve_mib < 0:
        raise ValueError('invalid reserved memory')
    if not isinstance(ram_mib, int) or isinstance(ram_mib, bool) or ram_mib <= 0 or ram_mib > 512:
        raise ValueError('guest RAM must be 1..512 MiB')
    if reserve_mib >= ram_mib:
        raise ValueError('reserved memory must leave at least one MiB for game pages')
    frames = (ram_mib - reserve_mib) * 1024 * 1024 // PAGE_BYTES
    resident = OrderedDict()
    history = {w: deque() for w in WINDOWS_US}
    counts = {w: {} for w in WINDOWS_US}
    peaks = {w: 0 for w in WINDOWS_US}
    above = {w: 0 for w in WINDOWS_US}
    seen = set()
    faults = evictions = dirty_writes = hits = writes = 0
    prev_t = -1
    count = 0
    peak_resident = 0
    for line in lines:
        if not line.strip():
            continue
        if count == max_events:
            raise ValueError('trace exceeds event cap')
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f'invalid JSON on event {count + 1}') from exc
        if not isinstance(row, dict) or set(row) != {'t_us', 'page', 'write'}:
            raise ValueError(f'event {count + 1} has invalid schema')
        t, page, write = row['t_us'], row['page'], row['write']
        if any(isinstance(n, bool) or not isinstance(n, int) or n < 0 or n > (1 << 63) - 1 for n in (t, page)) or not isinstance(write, bool):
            raise ValueError(f'event {count + 1} contains invalid values')
        if t < prev_t:
            raise ValueError(f'event {count + 1} has decreasing timestamp')
        prev_t = t
        count += 1
        seen.add(page)
        writes += int(write)
        for w in WINDOWS_US:
            q = history[w]
            freq = counts[w]
            cutoff = t - w
            while q and q[0][0] <= cutoff:
                _, old = q.popleft()
                freq[old] -= 1
                if freq[old] == 0:
                    del freq[old]
            q.append((t, page))
            freq[page] = freq.get(page, 0) + 1
            unique = len(freq)
            peaks[w] = max(peaks[w], unique)
            if unique > frames:
                above[w] += 1
        if page in resident:
            hits += 1
            was_dirty = resident.pop(page)
            resident[page] = was_dirty or write
        else:
            faults += 1
            if len(resident) == frames:
                _, was_dirty = resident.popitem(last=False)
                evictions += 1
                dirty_writes += int(was_dirty)
            resident[page] = write
        peak_resident = max(peak_resident, len(resident))
    return {
        'profile': {'guest_ram_mib': ram_mib, 'reserved_mib': reserve_mib,
                    'game_page_capacity': frames, 'page_bytes': PAGE_BYTES},
        'trace': {'events': count, 'distinct_pages_total': len(seen), 'write_references': writes},
        'lru_model': {'hits': hits, 'faults': faults, 'evictions': evictions,
                      'dirty_evictions': dirty_writes, 'minimum_page_in_bytes': faults * PAGE_BYTES,
                      'minimum_dirty_page_out_bytes': dirty_writes * PAGE_BYTES,
                      'peak_resident_pages': peak_resident},
        'trailing_window_pressure': {
            f'{w // 1000}ms': {'peak_distinct_pages': peaks[w],
                              'peak_distinct_bytes': peaks[w] * PAGE_BYTES,
                              'references_exceeding_page_capacity': above[w]}
            for w in WINDOWS_US},
        'evidence': 'offline simulated LRU and supplied trace only; no Warden runtime, FPS, page-fault latency, compression, CPU cost, swap bandwidth or guest RAM proof'
    }

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--guest-ram-mib', type=int, default=512)
    p.add_argument('--reserve-mib', type=int, default=64)
    p.add_argument('--output', type=Path)
    a = p.parse_args(argv)
    try:
        if not a.trace.is_file() or a.trace.stat().st_size > 128 * 1024 * 1024:
            raise ValueError('trace missing or exceeds 128 MiB')
        with a.trace.open(encoding='utf-8') as fh:
            report = analyze(fh, reserve_mib=a.reserve_mib, ram_mib=a.guest_ram_mib)
        result = json.dumps(report, indent=2, sort_keys=True) + '\n'
        if a.output:
            a.output.write_text(result, encoding='utf-8')
        else:
            print(result, end='')
        return 0
    except (OSError, ValueError, UnicodeError) as exc:
        print(f'TRACE REJECTED: {exc}', file=sys.stderr)
        return 2

if __name__ == '__main__':
    sys.exit(main())
