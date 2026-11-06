import unittest
from replay import analyze, InvalidTrace, MIB, PAGE

def sample(events, reserve_mib=511):
    return {
        "guest_ram_bytes": 512*MIB,
        "reserved_bytes": {"kernel":reserve_mib*MIB, "runtime":0, "framebuffers":0, "cache":0, "metadata":0},
        "events": events,
    }
def ev(ms, page, write=False):
    return {"ms":ms, "page":page, "write":write}

class ReplayTests(unittest.TestCase):
    def test_empty(self):
        r=analyze(sample([]))
        self.assertEqual((r["faults"],r["evictions"]), (0,0))
        self.assertEqual(r["resident_page_capacity"],256)
    def test_hit_and_fault(self):
        r=analyze(sample([ev(0,1),ev(1,1),ev(2,2)]))
        self.assertEqual((r["faults"],r["evictions"]), (2,0))
    def test_eviction_and_dirty_writeback(self):
        t=sample([ev(i,i,True) for i in range(257)])
        r=analyze(t)
        self.assertEqual((r["faults"],r["evictions"],r["dirty_evictions"]),(257,1,1))
        self.assertEqual(r["model_page_in_bytes"],257*PAGE)
    def test_lru_evicts_oldest_not_recent(self):
        events=[ev(i,i) for i in range(256)]+[ev(256,0),ev(257,256),ev(258,0),ev(259,1)]
        r=analyze(sample(events))
        self.assertEqual((r["faults"],r["evictions"]),(258,2))
    def test_unique_window(self):
        r=analyze(sample([ev(0,5),ev(5,5),ev(17,6),ev(35,7)]))
        self.assertEqual(r["window_unique_demand"]["16"]["observed_bins"],3)
        self.assertEqual(r["window_unique_demand"]["100"]["max_unique_pages"],3)
    def test_capacity_pressure(self):
        r=analyze(sample([ev(0,i) for i in range(257)]))
        self.assertEqual(r["window_unique_demand"]["16"]["bins_exceed_resident_capacity"],1)
    def test_reject_reserves(self):
        with self.assertRaises(InvalidTrace): analyze(sample([],512))
    def test_reject_non_monotonic(self):
        with self.assertRaises(InvalidTrace): analyze(sample([ev(9,0),ev(8,1)]))
    def test_reject_negative(self):
        with self.assertRaises(InvalidTrace): analyze(sample([ev(0,-1)]))
    def test_reject_bool_as_int(self):
        with self.assertRaises(InvalidTrace): analyze(sample([ev(True,1)]))
    def test_reject_wrong_flag(self):
        with self.assertRaises(InvalidTrace): analyze(sample([{"ms":0,"page":1,"write":1}]))
    def test_reject_wrong_profile(self):
        a=sample([])
        a["guest_ram_bytes"]=1024*MIB
        with self.assertRaises(InvalidTrace): analyze(a)
    def test_reject_unknown_reserve(self):
        a=sample([])
        a["reserved_bytes"]["extra"]=0
        with self.assertRaises(InvalidTrace): analyze(a)

if __name__ == "__main__":
    unittest.main()
