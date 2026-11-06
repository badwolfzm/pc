import unittest
from trace import TraceError, parse_events, lru_misses, optimal_misses, analyze, windows

class Tests(unittest.TestCase):
    def test_parsing(self):
        self.assertEqual(parse_events(['# header\n','0 7\n','200 8\n']),[(0,7),(200,8)])
    def test_bad_order(self):
        with self.assertRaises(TraceError): parse_events(['2 0','1 1'])
    def test_negative(self):
        with self.assertRaises(TraceError): parse_events(['0 -1'])
    def test_empty(self):
        with self.assertRaises(TraceError): parse_events([])
    def test_extra_fields(self):
        with self.assertRaises(TraceError): parse_events(['0 1 extra'])
    def test_zero_capacity(self):
        with self.assertRaises(TraceError): lru_misses([1],0)
    def test_belady_better(self):
        a=[1,2,3,1,2,4,1,2,3,4]
        self.assertEqual(optimal_misses(a,3),5)
        self.assertEqual(lru_misses(a,3),6)
    def test_first_touch(self):
        self.assertEqual(optimal_misses([1,1,2,2,1],2),2)
    def test_window(self):
        self.assertEqual(windows([(1,2),(15999,3),(16000,2)],16000),
                         [{'window_index':0,'distinct_pages':2},{'window_index':1,'distinct_pages':1}])
    def test_budget(self):
        r=analyze([(0,1),(1,2),(2,1)],64)
        self.assertEqual(r['resident_game_capacity_pages'],(512-64)*256)
        self.assertEqual(r['belady_offline_minimum_faults'],2)
    def test_bad_reserve(self):
        with self.assertRaises(TraceError): analyze([(0,1)],512)
    def test_window_unique_not_impossible(self):
        r=analyze([(0,i) for i in range(257)],511)
        self.assertEqual(r['windows_exceeding_capacity'],1)
        self.assertEqual(r['belady_offline_minimum_faults'],257)
    def test_exact_capacity(self):
        self.assertEqual(optimal_misses([1,2,3,1,2,3],3),3)
    def test_overflow(self):
        with self.assertRaises(TraceError): parse_events(['0 '+str(2**63)])

if __name__=='__main__':
    unittest.main()
