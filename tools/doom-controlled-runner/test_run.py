import hashlib
import pathlib
import sys
import tempfile
import unittest
from run import capture_command, check_file


class RunnerTests(unittest.TestCase):
    def test_small_output(self):
        r = capture_command([sys.executable, '-c', "print('hello')"], 5)
        self.assertEqual(r['returncode'], 0)
        self.assertEqual(r['log'], b'hello\n')
        self.assertFalse(r['timed_out'])
        self.assertFalse(r['log_overflow'])

    def test_exit_code(self):
        r = capture_command([sys.executable, '-c', 'raise SystemExit(7)'], 5)
        self.assertEqual(r['returncode'], 7)

    def test_overflow_kills(self):
        r = capture_command([sys.executable, '-u', '-c', "print('x'*30000,flush=True)"], 5, 100)
        self.assertTrue(r['log_overflow'])
        self.assertEqual(len(r['log']), 100)

    def test_timeout(self):
        r = capture_command([sys.executable, '-c', 'import time;time.sleep(5)'], 1)
        self.assertTrue(r['timed_out'])

    def test_invalid_timeout(self):
        for duration in (0, 601, True, 1.1):
            with self.subTest(duration=duration), self.assertRaises(ValueError):
                capture_command([sys.executable], duration)

    def test_invalid_limit(self):
        with self.assertRaises(ValueError):
            capture_command([sys.executable], 1, 0)

    def test_hash_pin(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / 'binary'
            p.write_bytes(b'test')
            digest = hashlib.sha256(b'test').hexdigest()
            self.assertEqual(check_file(p, digest, 'test')[1], digest)
            with self.assertRaisesRegex(ValueError, 'mismatch'):
                check_file(p, '0'*64, 'test')
            with self.assertRaisesRegex(ValueError, 'digest'):
                check_file(p, 'no', 'test')


if __name__ == '__main__':
    unittest.main()
