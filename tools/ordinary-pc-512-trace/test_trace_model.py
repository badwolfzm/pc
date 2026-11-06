import unittest
from trace_model import simulate

def sample(events, overhead=None):
    return {"non_game_resident_bytes": overhead or {"kernel": 64*1024*1024,
            "graphics": 32*1024*1024, "runtime": 16*1024*1024}, "events": events}

def ev(t, page, write=False):
    return {"t_ms": t, "page": page, "write": write}

class Tests(unittest.TestCase):
    def test_reuse(self):
        x = simulate(sample([ev(0,1),ev(1,1),ev(100,2)]))
        self.assertEqual((x["page_ins"], x["dirty_eviction_page_outs"]), (2,0))
        self.assertEqual(x["max_distinct_pages_per_100ms"], 1)
    def test_capacity_and_dirty(self):
        x = simulate(sample([ev(0,1,True),ev(1,2),ev(2,3)],
            {"reserved": 512*1024*1024 - 8192}))
        self.assertEqual((x["game_page_capacity"], x["page_ins"],
                          x["dirty_eviction_page_outs"]), (2,3,1))
    def test_lru(self):
        x = simulate(sample([ev(0,1,True),ev(1,2),ev(2,1),ev(3,3)],
            {"reserved": 512*1024*1024 - 8192}))
        self.assertEqual(x["dirty_eviction_page_outs"],0)
    def test_window_is_not_impossibility(self):
        x = simulate(sample([ev(0,1),ev(1,2),ev(2,3)],
            {"reserved": 512*1024*1024 - 8192}))
        self.assertTrue(x["working_set_exceeds_capacity_in_100ms_window"])
    def test_bad_overhead(self):
        for n in [0, -1, True, 512*1024*1024]:
            with self.subTest(n=n), self.assertRaises(ValueError):
                simulate(sample([ev(0,1)],{"reserved":n}))
    def test_bad_events(self):
        for events in [[],[ev(2,1),ev(1,2)],[{"t_ms":0,"page":1,"write":1}],
                       [ev(0,-1)],[{"t_ms":0,"page":1,"write":False,"extra":0}]]:
            with self.subTest(events=events), self.assertRaises(ValueError):
                simulate(sample(events))
    def test_deterministic(self):
        d=sample([ev(0,2,True),ev(1,3),ev(2,2)])
        self.assertEqual(simulate(d),simulate(d))

if __name__ == "__main__":
    unittest.main()
