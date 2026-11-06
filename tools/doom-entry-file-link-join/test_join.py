import unittest
from join import EXPECTED, PROTECTED, symbols, verify, verify_runtime_report

BASE = set(EXPECTED) | set(PROTECTED) | {'printf', 'doom_missing'}
AFTER = BASE - EXPECTED
EXTRAS = [('entry', {'_start'}, {'exit'}), ('exit', {'exit'}, set()),
          ('file', set(EXPECTED - {'exit'}), set())]


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

    def test_nm_parse(self):
        self.assertEqual(symbols('_start T 0 12\nfopen U\n'), ({'_start'}, {'fopen'}))

    def test_valid_join(self):
        self.assertEqual(set(verify(BASE, AFTER, {'doomgeneric_Create'}, EXTRAS)), EXPECTED)

    def test_missing_baseline(self):
        with self.assertRaises(ValueError):
            verify(BASE - {'fopen'}, AFTER, set(), EXTRAS)

    def test_unsafe_host_api_closed(self):
        with self.assertRaises(ValueError):
            verify(BASE, AFTER - {'system'}, set(), EXTRAS)

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
