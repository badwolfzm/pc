import tempfile
import unittest
from pathlib import Path
from analyze import analyze, read_csv


class WorkingSetTests(unittest.TestCase):
    def test_reuse_and_dirty_eviction(self):
        rows = [(str(i), str(i), 'W' if i == 0 else 'R') for i in range(257)]
        rows.append(('257', '256', 'R'))
        result = analyze(rows, total_mib=2, reserved_mib=1)
        self.assertEqual(result['usable_game_pages'], 256)
        self.assertEqual(result['page_faults'], 257)
        self.assertEqual(result['evictions'], 1)
        self.assertEqual(result['dirty_writebacks'], 1)
        self.assertEqual(result['references'], 258)

    def test_windows_are_time_based_and_unique(self):
        r = analyze([('0', '5', 'R'), ('10', '5', 'W'), ('16', '9', 'R'), ('26', '10', 'R')])
        self.assertEqual(r['peak_window_unique_pages']['16'], 2)
        self.assertEqual(r['peak_window_unique_pages']['100'], 3)
        self.assertEqual(r['unique_pages'], 3)

    def test_rejects_invalid(self):
        for rows in [[('1', '0', 'X')], [('2', '0', 'R'), ('1', '1', 'R')], [('0', '-1', 'R')], [('nan', '0', 'R')], []]:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                analyze(rows)
        with self.assertRaises(ValueError):
            analyze([('0', '0', 'R')], total_mib=512, reserved_mib=512)
        with self.assertRaises(ValueError):
            analyze([('0', '0', 'R')], page_size=3000)

    def test_csv_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'trace.csv'
            path.write_text('time_ms,page_id,access\n0,1,R\n1,1,W\n', encoding='utf-8')
            self.assertEqual(analyze(read_csv(path))['page_faults'], 1)
            path.write_text('page_id,time_ms,access\n1,0,R\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                analyze(read_csv(path))

    def test_capacity_warning_is_not_hard_impossibility(self):
        rows = [('0', str(p), 'R') for p in range(257)]
        r = analyze(rows, total_mib=2, reserved_mib=1)
        self.assertGreater(r['window_over_capacity_observations']['16'], 0)
        self.assertIn('not a mathematical impossibility', r['note'])


if __name__ == '__main__':
    unittest.main()
