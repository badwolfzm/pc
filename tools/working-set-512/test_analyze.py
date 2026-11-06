import itertools
import json
import tempfile
import unittest
from pathlib import Path
from analyze import analyze, load_trace, lru_faults, optimal_faults, window_unique

class WorkingSetTest(unittest.TestCase):
    def test_lru_and_optimal(self):
        events=list(enumerate((1,2,3,1,2,3)))
        self.assertEqual(lru_faults(events,2),6)
        self.assertEqual(optimal_faults(events,2),4)
    def test_repeated_page(self):
        events=[(0,1),(0,1),(16,1)]
        self.assertEqual(lru_faults(events,1),1)
        self.assertEqual(optimal_faults(events,1),1)
    def test_sliding_window(self):
        events=[(0,1),(15,2),(16,3)]
        self.assertEqual(window_unique(events,16),2)
        self.assertEqual(window_unique(events,100),3)
    def test_ram_bounds(self):
        self.assertEqual(analyze([(0,1)],1,0)["profile"]["available_page_frames"],256)
        for args in ((513,0),(512,512),(0,0)):
            with self.assertRaises(ValueError):
                analyze([(0,1)],*args)
    def test_invalid_trace(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/"trace.json"
            for events in ([{"t_ms":True,"page":1}],
                           [{"t_ms":2,"page":1},{"t_ms":1,"page":2}],
                           [{"t_ms":0,"page":-1}],[]):
                path.write_text(json.dumps({"events":events}))
                with self.assertRaises(ValueError):
                    load_trace(path)
    def test_valid_trace(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/"trace.json"
            path.write_text(json.dumps({"events":[{"t_ms":0,"page":1},{"t_ms":1,"page":2}]}))
            self.assertEqual(len(load_trace(path)),2)
    def test_optimal_never_worse_small_cases(self):
        for seq in itertools.product(range(3),repeat=5):
            events=list(enumerate(seq))
            self.assertLessEqual(optimal_faults(events,2),lru_faults(events,2))

if __name__=="__main__":
    unittest.main()
