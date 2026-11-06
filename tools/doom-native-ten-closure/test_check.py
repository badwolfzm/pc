import unittest
from check import EXPECTED, PROTECTED, verify

class TenClosureTests(unittest.TestCase):
    def setUp(self):
        self.before=set(EXPECTED)|set(PROTECTED)|{"printf","__errno_location"}
        self.after=self.before-set(EXPECTED)
    def test_exact_closure(self):
        v=verify(self.before,self.after)
        self.assertEqual((v["before_count"],v["after_count"]),(21,11))
        self.assertEqual(set(v["removed_symbols"]),set(EXPECTED))
        self.assertFalse(v["native_executable"])
    def test_missing_input_rejected(self):
        with self.assertRaisesRegex(ValueError,"lacks expected"):
            verify(self.before-{"malloc"},self.after)
    def test_missing_protected_input_rejected(self):
        with self.assertRaisesRegex(ValueError,"protected"):
            verify(self.before-{"system"},self.after)
    def test_extra_removed_rejected(self):
        with self.assertRaisesRegex(ValueError,"only ten"):
            verify(self.before,self.after-{"printf"})
    def test_protected_removed_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before,self.after-{"system"})
    def test_import_added_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before,self.after|{"new_import"})
    def test_missing_target_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before,self.after|{"fabs"})
if __name__=="__main__":
    unittest.main()
