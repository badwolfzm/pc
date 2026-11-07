import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch, Mock
from join import EXPECTED, PROTECTED, symbols, verify, verify_runtime_report, final_link_probe

BASE = set(EXPECTED) | set(PROTECTED) | {'printf', 'doom_missing'}
AFTER = BASE - EXPECTED
EXTRAS = [('entry', {'_start'}, {'exit'}), ('exit', {'exit'}, set()),
          ('file', {'fopen', 'fclose', 'fread', 'fseek', 'ftell'}, set()),
          ('console', {'printf', 'puts'}, {'vsnprintf'}),
          ('no_shell', {'system'}, {'__errno_location'})]


class JoinTests(unittest.TestCase):
    def test_canonical_eight_mib_report(self):
        report = {'arena_bytes_reserved_in_BSS': 8388608,
                  'pinned_default_doom_zone_bytes': 6291456,
                  'candidate_object_sha256': 'a' * 64,
                  'static_elf_allocatable_section_metrics': {
                      'allocatable_nobits_bytes': 8388608,
                      'allocatable_total_section_bytes': 9000000}}
        self.assertTrue(verify_runtime_report(report, 'a' * 64))
        for key, bad in [('arena_bytes_reserved_in_BSS', 1048576),
                         ('pinned_default_doom_zone_bytes', 0),
                         ('candidate_object_sha256', 'wrong')]:
            with self.subTest(key=key):
                changed = dict(report, **{key: bad})
                with self.assertRaises(ValueError):
                    verify_runtime_report(changed, 'a' * 64)
        for metrics in ({'allocatable_nobits_bytes': 0,
                         'allocatable_total_section_bytes': 9000000},
                        {'allocatable_nobits_bytes': 8388608,
                         'allocatable_total_section_bytes': 512 * 1024 * 1024}):
            with self.subTest(metrics=metrics):
                with self.assertRaises(ValueError):
                    verify_runtime_report(dict(report, static_elf_allocatable_section_metrics=metrics), 'a' * 64)
        with self.assertRaises(ValueError):
            verify_runtime_report(None, 'a' * 64)

    def test_scanner_report_is_exactly_bound_to_eight_mib_runtime(self):
        base = 'a' * 64
        scanned = 'b' * 64
        runtime = {
            'arena_bytes_reserved_in_BSS': 8388608,
            'pinned_default_doom_zone_bytes': 6291456,
            'candidate_object_sha256': base,
            'upstream_commit': 'e257a09527062bfb986b6e5e0780839b6cbd5b71',
            'static_elf_allocatable_section_metrics': {
                'allocatable_nobits_bytes': 8388608,
                'allocatable_total_section_bytes': 9000000}}
        scanner = {
            'original_bundle_object_sha256': base,
            'combined_object_sha256': scanned,
            'resolved_only': ['__isoc99_sscanf'],
            'scanner_exact_commit': '62dc9f73bee7753bd81d889a0e44ee8e95ce2220',
            'real_engine_upstream_commit': runtime['upstream_commit'],
            'final_executable_built': False}
        self.assertTrue(verify_runtime_report(runtime, scanned, scanner))
        for name, invalid in (
            ('original_bundle_object_sha256', 'wrong'),
            ('combined_object_sha256', 'wrong'),
            ('resolved_only', ['system']),
            ('scanner_exact_commit', 'wrong'),
            ('real_engine_upstream_commit', 'wrong'),
            ('final_executable_built', True)):
            with self.subTest(name=name):
                tampered = dict(scanner, **{name: invalid})
                with self.assertRaises(ValueError):
                    verify_runtime_report(runtime, scanned, tampered)
        with self.assertRaises(ValueError):
            verify_runtime_report(runtime, scanned)

    def test_no_shell_is_exactly_one_provider(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER | {'system'}, set(), EXTRAS)

    def test_real_final_link_negative_gate(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'native-doom.elf'
            fake_failure = Mock(returncode=1, stderr='ld: undefined reference to `stderr\'')
            with patch('join.subprocess.run', return_value=fake_failure) as runner:
                report = final_link_probe(Path(directory) / 'genuine.o',
                                          {'stderr', 'fwrite'}, output)
            self.assertEqual(report['entry'], '_start')
            self.assertEqual(report['remaining_imports'], ['fwrite', 'stderr'])
            self.assertFalse(report['executable_built'])
            cmd = runner.call_args.args[0]
            self.assertIn('_start', cmd)
            self.assertIn('--no-undefined', cmd)
            self.assertNotIn('-lc', cmd)

    def test_final_link_rejects_false_success(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'native-doom.elf'
            with patch('join.subprocess.run',
                       return_value=Mock(returncode=0, stderr='')):
                with self.assertRaises(ValueError):
                    final_link_probe(Path(directory) / 'genuine.o', {'stderr'}, output)

    def test_final_link_rejects_unrelated_failure(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'native-doom.elf'
            with patch('join.subprocess.run',
                       return_value=Mock(returncode=1, stderr='unknown linker option')):
                with self.assertRaises(ValueError):
                    final_link_probe(Path(directory) / 'genuine.o', {'stderr'}, output)

    def test_final_link_rejects_output_overwrite(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'native-doom.elf'
            output.write_bytes(b'protected')
            with self.assertRaises(ValueError):
                final_link_probe(Path(directory) / 'genuine.o', {'stderr'}, output)
            self.assertEqual(output.read_bytes(), b'protected')

    def test_final_link_refuses_fabricated_complete_input(self):
        with TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                final_link_probe(Path(directory) / 'genuine.o', set(),
                                 Path(directory) / 'candidate.elf')

    def test_nm_parse(self):
        self.assertEqual(symbols('_start T 0 12\nfopen U\n'), ({'_start'}, {'fopen'}))

    def test_valid_join(self):
        self.assertEqual(set(verify(BASE, AFTER, {'doomgeneric_Create'}, EXTRAS)), EXPECTED)

    def test_missing_baseline(self):
        with self.assertRaises(ValueError):
            verify(BASE - {'fopen'}, AFTER, set(), EXTRAS)

    def test_unsafe_host_api_closed(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER - {'remove'}, set(), EXTRAS)

    def test_new_import(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER | {'open'}, set(), EXTRAS)

    def test_duplicate_definition(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER, {'_start'}, EXTRAS)

    def test_duplicate_across_providers(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER, set(), EXTRAS + [('malicious', {'exit'}, set())])

    def test_missing_entry(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER, set(), [('file', set(EXPECTED), set())])

    def test_unclosed_exit(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER | {'exit'}, set(), EXTRAS)

    def test_unexpected_removal(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER - {'doom_missing'}, set(), EXTRAS)


if __name__ == '__main__':
    unittest.main()
