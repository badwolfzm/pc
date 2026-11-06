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
                  'candidate_object_sha256': 'abc',
                  'static_elf_allocatable_section_metrics': {
                      'allocatable_nobits_bytes': 8388608,
                      'allocatable_total_section_bytes': 9000000}}
        self.assertTrue(verify_runtime_report(report, 'abc'))
        for key, bad in [('arena_bytes_reserved_in_BSS', 1048576),
                         ('pinned_default_doom_zone_bytes', 0),
                         ('candidate_object_sha256', 'wrong')]:
            with self.subTest(key=key):
                changed = dict(report, **{key: bad})
                with self.assertRaises(ValueError):
                    verify_runtime_report(changed, 'abc')
        for metrics in ({'allocatable_nobits_bytes': 0,
                         'allocatable_total_section_bytes': 9000000},
                        {'allocatable_nobits_bytes': 8388608,
                         'allocatable_total_section_bytes': 512 * 1024 * 1024}):
            with self.subTest(metrics=metrics):
                with self.assertRaises(ValueError):
                    verify_runtime_report(dict(report, static_elf_allocatable_section_metrics=metrics), 'abc')
        with self.assertRaises(ValueError):
            verify_runtime_report(None, 'abc')

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
