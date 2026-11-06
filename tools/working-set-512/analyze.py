#!/usr/bin/env python3
"""Bounded, read-only 512-MiB working-set trace analyzer; not a game runner."""
import argparse
from collections import Counter, OrderedDict, defaultdict, deque
import heapq
import json
import sys

MAX_INPUT=8*1024*1024
MAX_EVENTS=150000
PAGE_BYTES=4096
WINDOWS=(16,100,1000)

def integer(x, name, low=0, high=10**12):
    if type(x) is not int or not low <= x <= high:
        raise ValueError(f"{name}: expected integer [{low}, {high}]")
    return x

def load_trace(path):
    with open(path,"rb") as f:
        raw=f.read(MAX_INPUT+1)
    if len(raw)>MAX_INPUT:
        raise ValueError("trace exceeds 8 MiB")
    obj=json.loads(raw)
    if type(obj) is not dict or type(obj.get("events")) is not list:
        raise ValueError("expected object with events list")
    seq=obj["events"]
    if not 1 <= len(seq) <= MAX_EVENTS:
        raise ValueError("event count out of range")
    events=[]
    previous=-1
    for i,item in enumerate(seq):
        if type(item) is not dict:
            raise ValueError(f"events[{i}]: expected object")
        t=integer(item.get("t_ms"),f"events[{i}].t_ms")
        p=integer(item.get("page"),f"events[{i}].page")
        if t < previous:
            raise ValueError("timestamps must be nondecreasing")
        previous=t
        events.append((t,p))
    return events

def window_unique(events,width):
    queue=deque()
    counts=Counter()
    maximum=0
    for t,p in events:
        queue.append((t,p))
        counts[p]+=1
        while queue and queue[0][0] <= t-width:
            _,old=queue.popleft()
            counts[old]-=1
            if not counts[old]:
                del counts[old]
        maximum=max(maximum,len(counts))
    return maximum

def lru_faults(events,capacity):
    resident=OrderedDict()
    misses=0
    for _,p in events:
        if p in resident:
            resident.move_to_end(p)
        else:
            misses+=1
            if len(resident)==capacity:
                resident.popitem(last=False)
            resident[p]=None
    return misses

def optimal_faults(events,capacity):
    # Offline MIN sees future accesses; theoretical comparison, not a pager.
    positions=defaultdict(deque)
    for i,(_,p) in enumerate(events):
        positions[p].append(i)
    inf=len(events)+1
    resident={}
    heap=[]
    misses=0
    for i,(_,p) in enumerate(events):
        positions[p].popleft()
        nxt=positions[p][0] if positions[p] else inf
        if p not in resident:
            misses+=1
            if len(resident)==capacity:
                while heap:
                    neg,victim=heapq.heappop(heap)
                    if resident.get(victim)==-neg:
                        del resident[victim]
                        break
                else:
                    raise RuntimeError("replacement heap invalid")
        resident[p]=nxt
        heapq.heappush(heap,(-nxt,p))
    return misses

def analyze(events,ram_mib=512,reserved_mib=128):
    ram_mib=integer(ram_mib,"ram_mib",1,512)
    reserved_mib=integer(reserved_mib,"reserved_mib",0,ram_mib-1)
    frames=(ram_mib-reserved_mib)*1024*1024//PAGE_BYTES
    distinct=len({p for _,p in events})
    lru=lru_faults(events,frames)
    opt=optimal_faults(events,frames)
    return {
        "profile":{"guest_ram_mib":ram_mib,"reserved_mib":reserved_mib,
                   "page_bytes":PAGE_BYTES,"available_page_frames":frames},
        "trace_events":len(events),"unique_pages":distinct,
        "unique_page_bytes":distinct*PAGE_BYTES,
        "max_distinct_pages_in_ms":{str(w):window_unique(events,w) for w in WINDOWS},
        "lru_page_misses":lru,"offline_optimal_page_misses":opt,
        "lru_extra_misses_over_optimal":lru-opt,
        "disclaimer":"Host-side model only; no real Warden page faults, I/O, FPS, or game compatibility measured. Offline optimum requires future knowledge."
    }

def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument("trace")
    a.add_argument("--ram-mib",type=int,default=512)
    a.add_argument("--reserved-mib",type=int,default=128)
    args=a.parse_args()
    try:
        print(json.dumps(analyze(load_trace(args.trace),args.ram_mib,args.reserved_mib),sort_keys=True,indent=2))
    except (ValueError,OSError,UnicodeError) as exc:
        print(f"trace refused: {exc}",file=sys.stderr)
        return 2
    return 0

if __name__=="__main__":
    sys.exit(main())
