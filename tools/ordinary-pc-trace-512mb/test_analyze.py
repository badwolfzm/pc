import json
import unittest
from analyze import analyze

def lines(*events):
    return [json.dumps(dict(t_us=t, page=p, write=w)) for t, p, w in events]

class Tests(unittest.TestCase):
    def test_empty(self):
        r = analyze([], reserve_mib=0, ram_mib=1)
        self.assertEqual(r['trace']['events'], 0)
        self.assertEqual(r['lru_model']['faults'], 0)

    def test_hits_faults(self):
        r = analyze(lines((0, 1, False), (1, 1, True), (2, 2, False)), reserve_mib=0, ram_mib=1)
        self.assertEqual((r['lru_model']['hits'], r['lru_model']['faults']), (1, 2))

    def test_lru_dirty_eviction(self):
        events = [(0, 0, True)] + [(i, i, False) for i in range(1, 257)]
        r = analyze(lines(*events), reserve_mib=0, ram_mib=1)
        self.assertEqual(r['lru_model']['dirty_evictions'], 1)
        self.assertEqual(r['lru_model']['evictions'], 1)
        self.assertEqual(r['lru_model']['minimum_dirty_page_out_bytes'], 4096)

    def test_window_boundaries(self):
        r = analyze(lines((0, 1, False), (15999, 2, False), (16000, 3, False)), reserve_mib=0, ram_mib=1)
        self.assertEqual(r['trailing_window_pressure']['16ms']['peak_distinct_pages'], 2)
        self.assertEqual(r['trailing_window_pressure']['100ms']['peak_distinct_pages'], 3)

    def test_repeated_page_not_overcounted(self):
        r = analyze(lines((0, 1, False), (1, 1, False), (2, 1, False)))
        self.assertEqual(r['trailing_window_pressure']['16ms']['peak_distinct_pages'], 1)

    def test_out_of_order(self):
        with self.assertRaisesRegex(ValueError, 'decreasing'):
            analyze(lines((3, 1, False), (2, 2, False)))

    def test_malformed_input(self):
        for x in ('{', '{}', '[]', '{"t_us":0,"page":1,"write":0}', '{"t_us":true,"page":1,"write":false}'):
            with self.subTest(x=x), self.assertRaises(ValueError):
                analyze([x])

    def test_reserve_over_cap(self):
        with self.assertRaises(ValueError):
            analyze([], reserve_mib=512)

    def test_duplicate_timestamps_allowed(self):
        r = analyze(lines((2, 1, False), (2, 2, False)))
        self.assertEqual(r['trailing_window_pressure']['16ms']['peak_distinct_pages'], 2)

    def test_cap(self):
        with self.assertRaisesRegex(ValueError, 'event cap'):
            analyze(lines((0, 1, False), (1, 2, False)), max_events=1)

if __name__ == '__main__':
    unittest.main()
