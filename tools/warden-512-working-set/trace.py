#!/usr/bin/env python3
"""Bounded page-reference trace model; not a game runner or kernel pager."""
import argparse
from collections import OrderedDict, defaultdict, deque
import json
import pathlib
import sys

PAGE = 4096
TOTAL_MIB = 512
MAX_EVENTS = 1000000
MAX_ID = 2**63 - 1

class TraceError(ValueError):
    pass

def parse_events(lines):
    events, last = [], -1
    for no, raw in enumerate(lines, 1):
        if not raw.strip() or raw.lstrip().startswith('#'):
            continue
        if len(raw) > 256:
            raise TraceError(f'line {no}: too long')
        parts = raw.split()
        if len(parts) != 2 or any(not x.isascii() or not x.isdecimal() for x in parts):
            raise TraceError(f'line {no}: expected unsigned timestamp_us page_id')
        t, p = map(int, parts)
        if t > MAX_ID or p > MAX_ID or t < last:
            raise TraceError(f'line {no}: nonmonotonic time or overflow')
        if len(events) >= MAX_EVENTS:
            raise TraceError('event limit exceeded')
        events.append((t,p))
        last = t
    if not events:
        raise TraceError('empty trace')
    return events

def lru_misses(pages, capacity):
    if capacity < 1:
        raise TraceError('capacity must be positive')
    cache = OrderedDict()
    faults = 0
    for page in pages:
        if page in cache:
            cache.move_to_end(page)
        else:
            faults += 1
            if len(cache) >= capacity:
                cache.popitem(last=False)
            cache[page] = None
    return faults

def optimal_misses(pages, capacity):
    """Belady MIN offline lower-bound on faults for this exact access order."""
    if capacity < 1:
        raise TraceError('capacity must be positive')
    future = defaultdict(deque)
    for i, page in enumerate(pages):
        future[page].append(i)
    resident = set()
    faults = 0
    for i, page in enumerate(pages):
        future[page].popleft()
        if page in resident:
            continue
        faults += 1
        if len(resident) >= capacity:
            victim = max(resident, key=lambda p:(future[p][0] if future[p] else len(pages), p))
            resident.remove(victim)
        resident.add(page)
    return faults

def windows(events, window_us):
    if window_us < 1:
        raise TraceError('window must be positive')
    bins = {}
    for timestamp, page in events:
        k = timestamp // window_us
        if k not in bins:
            bins[k] = set()
        bins[k].add(page)
    return [{'window_index':k, 'distinct_pages':len(v)} for k,v in sorted(bins.items())]

def analyze(events, reserved_mib, window_us=16000):
    if not isinstance(reserved_mib, int) or not 0 <= reserved_mib < TOTAL_MIB:
        raise TraceError('reserved MiB must be an integer 0..511')
    capacity = (TOTAL_MIB - reserved_mib) * 256
    pages = [page for _,page in events]
    buckets = windows(events, window_us)
    best = optimal_misses(pages, capacity)
    online = lru_misses(pages, capacity)
    return {
        'profile':'ordinary-pc-one-vcpu-512MiB',
        'trace_kind':'page-reference-only',
        'guest_total_mib':TOTAL_MIB,
        'non_game_reserved_mib':reserved_mib,
        'resident_game_capacity_pages':capacity,
        'page_bytes':PAGE,
        'references':len(events),
        'distinct_pages':len(set(pages)),
        'first_touch_minimum_faults':len(set(pages)),
        'belady_offline_minimum_faults':best,
        'lru_faults':online,
        'avoidable_lru_faults_vs_offline':online-best,
        'window_us':window_us,
        'max_window_unique_pages':max(b['distinct_pages'] for b in buckets),
        'windows_exceeding_capacity':sum(b['distinct_pages']>capacity for b in buckets),
        'window_count':len(buckets),
        'warning':('An offline trace model only: not a real active simultaneous working set, '
                   'runtime paging cost, frame-time/FPS result, or Warden gameplay. '
                   'Belady MIN has future knowledge. Window-unique pages above capacity '
                   'can still be paged sequentially. Assumed overhead reservation is NOT '
                   'a measured machine RAM accounting result.')
    }

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=pathlib.Path)
    parser.add_argument('--non-game-reserved-mib', type=int, required=True)
    parser.add_argument('--window-us', type=int, default=16000)
    args = parser.parse_args(argv)
    try:
        if not 1 <= args.window_us <= 1000000000:
            raise TraceError('window-us must be 1..1000000000')
        if args.trace.stat().st_size > 64 * 1024 * 1024:
            raise TraceError('trace exceeds 64 MiB')
        with args.trace.open('r',encoding='ascii') as f:
            events = parse_events(f)
        print(json.dumps({'ok':True,**analyze(events,args.non_game_reserved_mib,args.window_us)},sort_keys=True))
        return 0
    except (OSError, UnicodeError, TraceError) as e:
        print(json.dumps({'ok':False,'error':str(e)}),file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
