import unittest
from check_link import EXPECTED, SENSITIVE, symbols, verify
class Tests(unittest.TestCase):
    def test_nm(self):
        self.assertEqual(symbols(" U abs\n U system\n"),{"abs","system"})
        with self.assertRaises(ValueError): symbols("000 U abs")
        with self.assertRaises(ValueError): symbols(" U abs\n U abs")
    def test_exact(self):
        r=verify(EXPECTED|SENSITIVE|{"strncpy"},SENSITIVE|{"strncpy"})
        self.assertEqual(r["baseline_unresolved"]-r["after_pure_crt_unresolved"],2)
    def test_added_external_rejected(self):
        with self.assertRaises(ValueError): verify(EXPECTED|SENSITIVE,SENSITIVE|{"evil"})
    def test_sensitive_disappearance_rejected(self):
        with self.assertRaises(ValueError): verify(EXPECTED|SENSITIVE,SENSITIVE-{"system"})
    def test_missing_expected_rejected(self):
        with self.assertRaises(ValueError): verify(EXPECTED-{"abs"}|SENSITIVE,SENSITIVE)
if __name__=="__main__":unittest.main()
