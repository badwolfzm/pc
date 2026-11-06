import unittest
from run import extract_mm, MM
from proposed_fix import apply, START

class CandidatePatchTests(unittest.TestCase):
    def test_exact_baseline_replacement(self):
        source = extract_mm(MM.read_text())
        patched = apply(source)
        self.assertEqual(patched.count(START), 1)
        self.assertIn('n.checked_mul(PAGE)?', patched)
        self.assertIn('self.used = next_used;', patched)
        self.assertNotIn('for i in 1..n', patched)
        self.assertIn('pub struct FrameAlloc', patched)
    def test_refuses_already_patched_source(self):
        patched = apply(extract_mm(MM.read_text()))
        with self.assertRaises(ValueError):
            apply(patched)
    def test_refuses_wrong_function(self):
        with self.assertRaises(ValueError):
            apply('pub fn unrelated() {}')

if __name__ == '__main__':
    unittest.main()
