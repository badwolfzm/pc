#!/usr/bin/env python3
"""Offline page-residency study; NOT a Warden pager or a game benchmark."""
import argparse
import csv
import heapq
import json
from collections import OrderedDict
from pathlib import Path

MAX_ACCESSES = 200_000
MAX_PAGE_ID = (1 << 52) - 1

def capacity(total_mib, reserve_mib, page_bytes, meta_bytes):
    if not (1 <= total_mib <= 65536 and 0 <= reserve_mib < total_mib):
        raise ValueError('invalid total/reserve MiB')
    if page_bytes < 512 or page_bytes > 65536 or page_bytes & (page_bytes-1):
        raise ValueError('page size must be power of two from 512 to 65536')
    if not 0 <= meta_bytes <= 4096:
        raise ValueError('invalid per-frame metadata')
    return ((total_mib - reserve_mib)*1048576)//(page_bytes + meta_bytes)

def parse_trace(path):
    rows = []
    with Path(path).open(newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != ['page', 'op']:
            raise ValueError('expected exact CSV header: page,op')
        for row in reader:
            if len(rows) == MAX_ACCESSES:
                raise ValueError('trace exceeds 200000 accesses')
            if None in row or row['page'] is None or row['op'] not in ('R','W'):
                raise ValueError('invalid trace record')
            page_s = row['page']
            if not page_s or page_s.strip() != page_s or page_s[0] == '-':
                raise ValueError('invalid page id')
            try:
                page = int(page_s, 0) if page_s.startswith(('0x','0X')) else int(page_s, 10)
            except ValueError as exc:
                raise ValueError('invalid page id') from exc
            if not 0 <= page <= MAX_PAGE_ID:
                raise ValueError('page id outside range')
            rows.append((page,row['op']=='W'))
    if not rows:
        raise ValueError('empty trace')
    return rows

def simulate(rows, cap, policy, read_us, write_us, page_bytes=4096):
    if cap < 1:
        raise ValueError('page capacity must be >= 1')
    if policy not in ('fifo','lru','opt'):
        raise ValueError('bad policy')
    if read_us < 0 or write_us < 0:
        raise ValueError('negative IO cost')
    frames = OrderedDict()
    faults=writebacks=hits=peak=0
    if policy == 'opt':
        # Theoretical MIN replacement: future knowledge cannot be used at runtime.
        next_at=[len(rows)+1]*len(rows)
        future={}
        for i in range(len(rows)-1,-1,-1):
            p=rows[i][0]
            next_at[i]=future.get(p,len(rows)+1)
            future[p]=i
        best={} ; heap=[]
        for i,(p,w) in enumerate(rows):
            if p in frames:
                hits+=1
                frames[p]=frames[p] or w
            else:
                faults+=1
                if len(frames)==cap:
                    while heap:
                        neg_future, neg_order, victim = heapq.heappop(heap)
                        if victim in frames and best[victim] == (-neg_future,-neg_order):
                            break
                    else:
                        raise AssertionError('missing resident victim')
                    writebacks+=bool(frames.pop(victim))
                    del best[victim]
                frames[p]=w
            best[p]=(next_at[i], i)
            heapq.heappush(heap,(-next_at[i],-i,p))
            peak=max(peak,len(frames))
    else:
        for p,w in rows:
            if p in frames:
                hits+=1
                frames[p]=frames[p] or w
                if policy=='lru': frames.move_to_end(p)
            else:
                faults+=1
                if len(frames)==cap:
                    _,dirty=frames.popitem(last=False)
                    writebacks+=int(dirty)
                frames[p]=w
            peak=max(peak,len(frames))
    return {'policy': policy,'accesses':len(rows),'distinct_pages':len(set(p for p,_ in rows)),
            'resident_capacity_pages':cap,'peak_resident_pages':peak,'hits':hits,'faults':faults,
            'dirty_writebacks':writebacks,'read_bytes':faults*page_bytes,'write_bytes':writebacks*page_bytes,
            'estimated_io_stall_us':faults*read_us+writebacks*write_us,
            'hit_ratio':round(hits/len(rows),6),
            'note':'OPT is an impossible future-aware lower bound' if policy=='opt' else 'offline trace replay; not a real guest run'}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('trace',help='CSV with exact header page,op; R/W and decimal/hex page IDs')
    ap.add_argument('--total-mib',type=int,default=512)
    ap.add_argument('--reserve-mib',type=int,default=192)
    ap.add_argument('--page-bytes',type=int,default=4096)
    ap.add_argument('--frame-meta-bytes',type=int,default=64)
    ap.add_argument('--read-us',type=int,default=100)
    ap.add_argument('--write-us',type=int,default=100)
    a=ap.parse_args()
    cap=capacity(a.total_mib,a.reserve_mib,a.page_bytes,a.frame_meta_bytes)
    rows=parse_trace(a.trace)
    results=[simulate(rows,cap,p,a.read_us,a.write_us,a.page_bytes) for p in ('fifo','lru','opt')]
    print(json.dumps({'kind':'offline_trace_replay_not_native_warden','total_guest_mib':a.total_mib,
      'reserved_guest_mib':a.reserve_mib,'frame_meta_bytes':a.frame_meta_bytes,'page_bytes':a.page_bytes,
      'assumed_read_us':a.read_us,'assumed_write_us':a.write_us,'results':results},indent=2,sort_keys=True))

if __name__=='__main__':
    main()
