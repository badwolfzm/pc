import argparse
import os
import sys
import unittest
from run import positive_int, run


@unittest.skipUnless(sys.platform.startswith('linux') and hasattr(os, 'sched_getaffinity'), 'Linux only')
class EnvelopeTests(unittest.TestCase):
    def cpu(self):
        return min(os.sched_getaffinity(0))

    def test_success(self):
        result = run([sys.executable, '-c', 'print(1)'], 256, 3, self.cpu())
        self.assertEqual(result['exit_code'], 0)
        self.assertFalse(result['timed_out'])

    def test_child_is_pinned_to_one_cpu(self):
        script = 'import os; assert os.sched_getaffinity(0) == {' + str(self.cpu()) + '}'
        result = run([sys.executable, '-c', script], 256, 3, self.cpu())
        self.assertEqual(result['exit_code'], 0)
        self.assertFalse(result['timed_out'])

    def test_failing_child(self):
        result = run([sys.executable, '-c', 'raise SystemExit(7)'], 256, 3, self.cpu())
        self.assertEqual(result['exit_code'], 7)

    def test_timeout(self):
        result = run([sys.executable, '-c', 'import time; time.sleep(30)'], 256, 1, self.cpu())
        self.assertTrue(result['timed_out'])
        self.assertEqual(result['exit_code'], -9)

    def test_wrong_cpu(self):
        with self.assertRaises(ValueError):
            run(['/bin/true'], 64, 1, max(os.sched_getaffinity(0)) + 100)

    def test_address_space_limit(self):
        code = 'x = bytearray(512 * 1024 * 1024)'
        result = run([sys.executable, '-c', code], 128, 3, self.cpu())
        self.assertNotEqual(result['exit_code'], 0)

    def test_positive_parser(self):
        for invalid in ('0', '-2', 'abc', str(2**31)):
            with self.assertRaises(argparse.ArgumentTypeError):
                positive_int(invalid)
        self.assertEqual(positive_int('64'), 64)


if __name__ == '__main__':
    unittest.main()
