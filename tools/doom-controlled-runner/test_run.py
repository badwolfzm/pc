import hashlib
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch
from run import capture_command, check_file, run


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

    def run_fixture(self, folder, log, report):
        binary = pathlib.Path(folder) / 'qemu-system-x86_64'
        kernel = pathlib.Path(folder) / 'kernel.mb32'
        binary.write_bytes(b'fake qemu for test only')
        kernel.write_bytes(b'fake kernel for test only')
        digest_qemu = hashlib.sha256(binary.read_bytes()).hexdigest()
        digest_kernel = hashlib.sha256(kernel.read_bytes()).hexdigest()
        with patch('run.check_args', return_value={'vcpus_requested': 1}), \
             patch('run.capture_command', return_value={'returncode': 1, 'timed_out': False,
                                                        'log_overflow': False, 'log': b'guest log'}):
            return run(binary, digest_qemu, kernel, digest_kernel, 'tcg', 2, log, report)

    def test_debug_exit_one_is_expected_guest_zero(self):
        with tempfile.TemporaryDirectory() as d:
            with patch('run.capture_command', return_value={
                'returncode': 1, 'timed_out': False,
                'log_overflow': False, 'log': b'debug-exit guest zero'}):
                self.assertEqual(self.run_fixture_with_patched_capture(d), 0)
            report = __import__('json').loads((pathlib.Path(d) / 'out.json').read_text())
            self.assertEqual(report['debug_exit_status'], 0)
            self.assertTrue(report['warden_debug_exit_zero'])
            self.assertFalse(report['guest_boot_verified'])

    def test_host_zero_not_guest_debug_exit(self):
        with tempfile.TemporaryDirectory() as d:
            with patch('run.capture_command', return_value={
                'returncode': 0, 'timed_out': False,
                'log_overflow': False, 'log': b'host exited'}):
                self.assertEqual(self.run_fixture_with_patched_capture(d), 1)
            report = __import__('json').loads((pathlib.Path(d) / 'out.json').read_text())
            self.assertIsNone(report['debug_exit_status'])
            self.assertFalse(report['warden_debug_exit_zero'])

    def test_debug_exit_three_is_failure(self):
        with tempfile.TemporaryDirectory() as d:
            with patch('run.capture_command', return_value={
                'returncode': 3, 'timed_out': False,
                'log_overflow': False, 'log': b'debug-exit guest one'}):
                self.assertEqual(self.run_fixture_with_patched_capture(d), 1)
            report = __import__('json').loads((pathlib.Path(d) / 'out.json').read_text())
            self.assertEqual(report['debug_exit_status'], 1)

    def run_fixture_with_patched_capture(self, folder):
        binary = pathlib.Path(folder) / 'qemu-system-x86_64'
        kernel = pathlib.Path(folder) / 'kernel.mb32'
        binary.write_bytes(b'fake qemu for test only')
        kernel.write_bytes(b'fake kernel for test only')
        with patch('run.check_args', return_value={'vcpus_requested': 1}):
            return run(binary, hashlib.sha256(binary.read_bytes()).hexdigest(),
                       kernel, hashlib.sha256(kernel.read_bytes()).hexdigest(),
                       'tcg', 2, pathlib.Path(folder) / 'out.log',
                       pathlib.Path(folder) / 'out.json')

    def test_exclusive_outputs_and_evidence(self):
        with tempfile.TemporaryDirectory() as d:
            log = pathlib.Path(d) / 'out.log'
            report = pathlib.Path(d) / 'out.json'
            self.assertEqual(self.run_fixture(d, log, report), 0)
            self.assertEqual(log.read_bytes(), b'guest log')
            self.assertFalse(__import__('json').loads(report.read_text())['doom_executed_verified'])

    def test_existing_log_never_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            log = pathlib.Path(d) / 'out.log'
            report = pathlib.Path(d) / 'out.json'
            log.write_text('keep this')
            with self.assertRaises(FileExistsError):
                self.run_fixture(d, log, report)
            self.assertEqual(log.read_text(), 'keep this')
            self.assertFalse(report.exists())

    def test_symlink_log_never_follows_target(self):
        with tempfile.TemporaryDirectory() as d:
            target = pathlib.Path(d) / 'private.txt'
            target.write_text('untouched')
            log = pathlib.Path(d) / 'out.log'
            log.symlink_to(target)
            with self.assertRaises(FileExistsError):
                self.run_fixture(d, log, pathlib.Path(d) / 'out.json')
            self.assertEqual(target.read_text(), 'untouched')


if __name__ == '__main__':
    unittest.main()
