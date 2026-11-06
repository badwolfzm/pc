import unittest
from run import extract_mm, MM
from proposed_fix import apply, START

def live_source():
    return extract_mm(MM.read_text())

def vulnerable_source():
    src = live_source()
    if "let first = self.alloc()?;" not in src:
        raise unittest.SkipTest("kernel source was repaired; original proposal no longer applicable")
    return src

class CandidatePatchTests(unittest.TestCase):
    def test_exact_baseline_replacement(self):
        patched = apply(vulnerable_source())
        self.assertEqual(patched.count(START), 1)
        self.assertIn('n.checked_mul(PAGE)?', patched)
        self.assertIn('self.used = next_used;', patched)
        self.assertNotIn('for i in 1..n', patched)
        self.assertIn('pub struct FrameAlloc', patched)
    def test_refuses_already_patched_source(self):
        patched = apply(vulnerable_source())
        with self.assertRaises(ValueError):
            apply(patched)
    def test_refuses_repaired_kernel_source(self):
        # Once mm.rs is changed, candidate source replacement must fail closed
        # rather than overwrite the reviewed production allocator.
        src = live_source()
        if "let first = self.alloc()?;" in src:
            src = apply(src)
        with self.assertRaises(ValueError):
            apply(src)
    def test_refuses_wrong_function(self):
        with self.assertRaises(ValueError):
            apply('pub fn unrelated() {}')

if __name__ == '__main__':
    unittest.main()
