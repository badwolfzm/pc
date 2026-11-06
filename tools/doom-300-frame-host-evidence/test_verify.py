import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parent))
from verify import verify

def frame(i, stamp, digest):
    return f"FRAME,{i},{stamp},{digest:016x}\n"

class FrameEvidenceTests(unittest.TestCase):
    def valid(self):
        return [
            "upstream Doom startup log\n",
            frame(0, 100, 1),
            frame(1, 130, 2),
            frame(2, 161, 3),
            frame(3, 190, 3),
            "FRAME_COUNT,4\n",
        ]

    def test_valid_timed_frame_evidence(self):
        evidence = verify(self.valid(), required=4)
        self.assertEqual(evidence["frames"], 4)
        self.assertEqual(evidence["distinct_frame_hashes"], 3)
        self.assertEqual(evidence["interval_ms_p95"], 31)
        self.assertGreater(evidence["observed_paced_callback_hz"], 0)

    def test_missing_and_reordered_frames(self):
        for lines in [self.valid()[:-2] + ["FRAME_COUNT,4\n"],
                      self.valid()[:-1] + [frame(1, 200, 4), "FRAME_COUNT,4\n"],
                      self.valid()[:-1] + ["FRAME_COUNT,5\n"]]:
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                verify(lines, 4)

    def test_constant_hash_and_stalled_clock(self):
        constant = [frame(i, 100+i*30, 42) for i in range(4)] + ["FRAME_COUNT,4\n"]
        stalled = [frame(i, 100, i) for i in range(4)] + ["FRAME_COUNT,4\n"]
        for lines in [constant, stalled]:
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                verify(lines, 4)

    def test_bad_format_duplicate_count_and_invalid_budget(self):
        bad = [frame(0,100,1), "FRAME,1,130,NOTHEX\n",
               frame(2,160,2), frame(3,190,3), "FRAME_COUNT,4\n"]
        for lines in [bad, self.valid() + ["FRAME_COUNT,4\n"]]:
            with self.assertRaises(ValueError):
                verify(lines, 4)
        with self.assertRaises(ValueError):
            verify(self.valid(), 2)

if __name__ == "__main__":
    unittest.main()
