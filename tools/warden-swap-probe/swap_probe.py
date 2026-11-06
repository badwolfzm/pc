"""Host-only, bounded page-swap reference model; NOT kernel-integrated."""
import argparse
import hashlib
import json
import os
import tempfile
import time
from collections import OrderedDict

PAGE_SIZE = 4096


class SwapFault(Exception):
    pass


class Pager:
    def __init__(self, memory_pages, swap_pages, directory=None):
        if not isinstance(memory_pages, int) or not 1 <= memory_pages <= 65536:
            raise ValueError('memory_pages out of bounds')
        if not isinstance(swap_pages, int) or not 1 <= swap_pages <= 65536:
            raise ValueError('swap_pages out of bounds')
        self.memory_pages = memory_pages
        self.swap_pages = swap_pages
        self._resident = OrderedDict()
        self._slots = {}
        self._free = list(range(swap_pages - 1, -1, -1))
        self._pages = set()
        self._stats = {'reads': 0, 'writes': 0, 'faults': 0, 'evictions': 0, 'hits': 0}
        self._file = tempfile.TemporaryFile(dir=directory)
        self._closed = False

    def _check(self, ident):
        if self._closed:
            raise SwapFault('pager closed')
        if not isinstance(ident, int) or ident < 0 or ident >= 1 << 32:
            raise ValueError('page identifier out of bounds')
        if ident not in self._pages:
            raise SwapFault('unknown page')

    def alloc(self, ident):
        if self._closed:
            raise SwapFault('pager closed')
        if not isinstance(ident, int) or ident < 0 or ident >= 1 << 32:
            raise ValueError('page identifier out of bounds')
        if ident in self._pages:
            raise SwapFault('duplicate page')
        if len(self._pages) >= self.memory_pages + self.swap_pages:
            raise SwapFault('virtual page capacity exhausted')
        self._ensure_room()
        self._pages.add(ident)
        self._resident[ident] = [bytes(PAGE_SIZE), False]

    def _ensure_room(self):
        if len(self._resident) < self.memory_pages:
            return
        victim, (data, dirty) = next(iter(self._resident.items()))
        if dirty or victim not in self._slots:
            if victim in self._slots:
                slot, _ = self._slots[victim]
            else:
                if not self._free:
                    raise SwapFault('swap full')
                slot = self._free.pop()
            try:
                self._file.seek(slot * PAGE_SIZE)
                count = self._file.write(data)
                self._file.flush()
                if count != PAGE_SIZE:
                    raise OSError('short swap write')
            except OSError as exc:
                if victim not in self._slots:
                    self._free.append(slot)
                raise SwapFault('swap write failure') from exc
            self._slots[victim] = (slot, hashlib.sha256(data).digest())
            self._stats['writes'] += 1
        del self._resident[victim]
        self._stats['evictions'] += 1

    def read(self, ident):
        self._check(ident)
        if ident in self._resident:
            self._resident.move_to_end(ident)
            self._stats['hits'] += 1
            return self._resident[ident][0]
        if ident not in self._slots:
            raise SwapFault('no backing slot')
        slot, expected = self._slots[ident]
        self._file.seek(slot * PAGE_SIZE)
        data = self._file.read(PAGE_SIZE)
        if len(data) != PAGE_SIZE or hashlib.sha256(data).digest() != expected:
            raise SwapFault('swap content corrupt or truncated')
        # Recycle the incoming page's slot if the swap area is full.
        self._slots.pop(ident)
        self._free.append(slot)
        try:
            self._ensure_room()
        except (SwapFault, OSError):
            self._free.remove(slot)
            self._slots[ident] = (slot, expected)
            raise
        self._resident[ident] = [data, False]
        self._stats['faults'] += 1
        self._stats['reads'] += 1
        return data

    def write(self, ident, payload):
        if not isinstance(payload, bytes) or len(payload) != PAGE_SIZE:
            raise ValueError('write requires exactly one 4096-byte page')
        self.read(ident)
        self._resident[ident] = [payload, True]

    def free(self, ident):
        self._check(ident)
        self._resident.pop(ident, None)
        if ident in self._slots:
            slot, _ = self._slots.pop(ident)
            self._free.append(slot)
        self._pages.remove(ident)

    def stats(self):
        return dict(self._stats, resident=len(self._resident), swapped=len(self._slots),
                    allocated=len(self._pages), resident_bytes=len(self._resident) * PAGE_SIZE,
                    swap_capacity_bytes=self.swap_pages * PAGE_SIZE)

    def close(self):
        if not self._closed:
            self._file.close()
            self._closed = True
            self._resident.clear()
            self._slots.clear()
            self._pages.clear()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def bench(pages, ram, rounds):
    if pages <= ram or pages > ram + 65536 or rounds < 1 or rounds > 100:
        raise ValueError('invalid benchmark configuration')
    with Pager(ram, pages - ram) as pager:
        t0 = time.perf_counter_ns()
        for p in range(pages):
            pager.alloc(p)
            pager.write(p, bytes([p & 255]) * PAGE_SIZE)
        for _ in range(rounds):
            for p in range(pages):
                if pager.read(p)[0] != (p & 255):
                    raise SwapFault('incorrect restored page')
        elapsed = (time.perf_counter_ns() - t0) / 1e9
        return {'elapsed_seconds': round(elapsed, 6), 'operations': pages * (rounds + 2),
                'pages_per_second': round(pages * rounds / elapsed, 1),
                'page_size': PAGE_SIZE, 'host_only': True, **pager.stats()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pages', type=int, default=256)
    ap.add_argument('--ram-pages', type=int, default=64)
    ap.add_argument('--rounds', type=int, default=3)
    a = ap.parse_args()
    try:
        print(json.dumps(bench(a.pages, a.ram_pages, a.rounds), sort_keys=True))
    except (SwapFault, ValueError, OSError) as exc:
        ap.error(str(exc))


if __name__ == '__main__':
    main()
