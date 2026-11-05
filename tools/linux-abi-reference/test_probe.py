"""Exercise the real native syscall probe, including a deliberate wrong oracle."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent

class ProbeTests(unittest.TestCase):
    def build_run(self, source):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'probe.c'
            exe = Path(tmp) / 'probe'
            src.write_text(source)
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                str(src), '-o', str(exe)], check=True, timeout=30)
            return subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)

    def test_real_native_cases_and_json(self):
        self.assertTrue((ROOT / 'probe.c').is_file(), 'native probe implementation is missing')
        run = self.build_run((ROOT / 'probe.c').read_text())
        self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
        report = json.loads(run.stdout)
        self.assertEqual(report['execution'], 'native-linux-reference')
        self.assertEqual(report['schema'], 1)
        cases = {row['name']: row for row in report['cases']}
        required = {
            'affinity_self', 'affinity_missing_pid', 'affinity_short',
            'affinity_unaligned', 'affinity_unmapped', 'affinity_readonly',
            'rlimit_self', 'rlimit_invalid_resource', 'rlimit_unmapped',
            'rlimit_readonly', 'rlimit_cross_page', 'prlimit_self',
            'prlimit_missing_pid', 'clock_invalid', 'clock_nullable', 'clock_readonly',
        }
        self.assertEqual(set(cases), required)
        self.assertTrue(all(row['passed'] for row in cases.values()))
        self.assertEqual(report['failed'], 0)
        self.assertEqual(cases['affinity_missing_pid']['errno'], 3)
        self.assertEqual(cases['affinity_unaligned']['errno'], 22)
        self.assertEqual(cases['rlimit_cross_page']['errno'], 14)
        for name in ['affinity_missing_pid', 'affinity_short', 'affinity_unaligned']:
            self.assertTrue(cases[name]['output_unchanged'])

    def test_wrong_expectation_fails_process_and_report(self):
        self.assertTrue((ROOT / 'probe.c').is_file(), 'native probe implementation is missing')
        source = (ROOT / 'probe.c').read_text()
        needle = 'const int missing_pid_errno = ESRCH;'
        self.assertIn(needle, source)
        run = self.build_run(source.replace(needle, 'const int missing_pid_errno = EINVAL;'))
        self.assertEqual(run.returncode, 1, run.stderr + run.stdout)
        report = json.loads(run.stdout)
        self.assertGreater(report['failed'], 0)
        cases = {row['name']: row for row in report['cases']}
        self.assertFalse(cases['affinity_missing_pid']['passed'])

    def test_mutated_output_marks_each_affected_row_failed(self):
        source = (ROOT / 'probe.c').read_text()
        needle = 'int unchanged = check_sentinel ? sentinel_intact() : -1;'
        self.assertIn(needle, source)
        run = self.build_run(source.replace(
            needle, 'if (check_sentinel) mask[0] = 0;\n    ' + needle))
        self.assertEqual(run.returncode, 1, run.stderr + run.stdout)
        report = json.loads(run.stdout)
        bad_rows = [row for row in report['cases'] if not row['passed']]
        self.assertEqual(report['failed'], len(bad_rows))
        self.assertEqual(len(bad_rows), 3)
        self.assertTrue(all(row['output_unchanged'] is False for row in bad_rows))

if __name__ == '__main__':
    unittest.main()
