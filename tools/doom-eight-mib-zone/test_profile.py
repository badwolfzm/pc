import importlib.util
import pathlib
import unittest

FILE = pathlib.Path(__file__).with_name("profile.py")
spec = importlib.util.spec_from_file_location("profile", FILE)
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)

CANON = 'begin; "-DDOOM_ARENA_BYTES=1048576"\n' + (
    '"arena_bytes_reserved_in_BSS": 1048576\n__file__ FORMAT_MERGED_COMMIT')
class ProfileTests(unittest.TestCase):
    def test_exact_two_scalar_changes(self):
        updated = p.checked_transform(CANON)
        self.assertIn("8388608", updated)
        self.assertNotIn("1048576", updated)
        self.assertEqual(updated.replace("8388608", "1048576"), CANON)

    def test_missing_occurrence_fails(self):
        with self.assertRaisesRegex(ValueError, "changed"):
            p.checked_transform(CANON.replace('"-DDOOM_ARENA_BYTES=1048576"', "removed"))

    def test_multiple_occurrences_fail(self):
        with self.assertRaisesRegex(ValueError, "changed"):
            p.checked_transform(CANON + '\n' + '"arena_bytes_reserved_in_BSS": 1048576')

    def test_minimum_real_zone_headroom(self):
        self.assertEqual(p.ZONE, 6291456)
        self.assertEqual(p.CAPACITY, 8388608)
        self.assertGreater(p.CAPACITY, p.ZONE)

if __name__ == "__main__":
    unittest.main()
