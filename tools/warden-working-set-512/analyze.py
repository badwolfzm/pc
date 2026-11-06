#!/usr/bin/env python3
"""Analyze an authorized timestamped page trace under a total-guest-RAM cap."""
import argparse
from collections import Counter, OrderedDict, deque
import csv
import json
from pathlib import Path

PAGE = 4096
WINDOWS_MS = (16, 100, 1000)


def analyze(rows, total_mib=512, reserved_mib=128, page_size=PAGE):
    if not isinstance(total_mib, int) or not isinstance(reserved_mib, int) or total_mib <= 0 or not 0 <= reserved_mib < total_mib:
        raise ValueError('total and reserved MiB must be integers, with 0 <= reserved < total')
    if not isinstance(page_size, int) or page_size <= 0 or page_size & (page_size - 1):
        raise ValueError('page_size must be a positive power of two')
    cap = ((total_mib - reserved_mib) * 1024 * 1024) // page_size
    if cap < 1:
        raise ValueError('no pages remain after reservation')
    cache = OrderedDict()  # page -> dirty bool
    windows = {ms: (deque(), Counter()) for ms in WINDOWS_MS}
    peaks = {ms: 0 for ms in WINDOWS_MS}
    over = {ms: 0 for ms in WINDOWS_MS}
    counts = {'references': 0, 'page_faults': 0, 'evictions': 0, 'dirty_writebacks': 0}
    previous = None
    unique = set()
    for row in rows:
        try:
            raw_time, raw_page, access = row
            time = int(raw_time)
            page = int(raw_page)
        except (TypeError, ValueError) as exc:
            raise ValueError('trace rows must be time_ms,page_id,access') from exc
        if str(raw_time) != str(time) or str(raw_page) != str(page) or time < 0 or page < 0 or access not in ('R', 'W'):
            raise ValueError('invalid timestamp, page, or access (R/W)')
        if previous is not None and time < previous:
            raise ValueError('timestamps must be nondecreasing')
        previous = time
        counts['references'] += 1
        unique.add(page)
        if page in cache:
            cache[page] = cache[page] or access == 'W'
            cache.move_to_end(page)
        else:
            counts['page_faults'] += 1
            if len(cache) == cap:
                _, dirty = cache.popitem(last=False)
                counts['evictions'] += 1
                if dirty:
                    counts['dirty_writebacks'] += 1
            cache[page] = access == 'W'
        for ms, (queue, frequency) in windows.items():
            # Inclusive window [time-ms+1, time], with same-time events retained.
            while queue and time - queue[0][0] >= ms:
                _, old = queue.popleft()
                frequency[old] -= 1
                if frequency[old] == 0:
                    del frequency[old]
            queue.append((time, page))
            frequency[page] += 1
            n = len(frequency)
            peaks[ms] = max(peaks[ms], n)
            if n > cap:
                over[ms] += 1
    if counts['references'] == 0:
        raise ValueError('empty trace')
    return {
        'scope': 'host trace simulation; NOT Warden guest measurement',
        'total_guest_ram_mib': total_mib, 'reserved_non_game_mib': reserved_mib,
        'usable_game_pages': cap, 'page_size_bytes': page_size,
        **counts, 'unique_pages': len(unique),
        'peak_window_unique_pages': {str(k): peaks[k] for k in WINDOWS_MS},
        'window_over_capacity_observations': {str(k): over[k] for k in WINDOWS_MS},
        'note': 'Window uniqueness above capacity indicates working-set pressure, not a mathematical impossibility: pages may be accessed sequentially. LRU sim does not model I/O latency, bandwidth, compression or GPU memory.'
    }


def read_csv(path):
    with Path(path).open(newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != ['time_ms', 'page_id', 'access']:
            raise ValueError('CSV header must be time_ms,page_id,access')
        for row in reader:
            if None in row or any(v is None for v in row.values()):
                raise ValueError('invalid CSV row')
            yield row['time_ms'], row['page_id'], row['access']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', help='authorized CSV: time_ms,page_id,access')
    p.add_argument('--total-mib', type=int, default=512)
    p.add_argument('--reserved-mib', type=int, default=128)
    a = p.parse_args()
    try:
        result = analyze(read_csv(a.trace), total_mib=a.total_mib, reserved_mib=a.reserved_mib)
    except (ValueError, OSError) as exc:
        p.error(str(exc))
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
