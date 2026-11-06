import unittest
from check_link import symbols, verify, EXPECTED, SENSITIVE
class Tests(unittest.TestCase):
    def test_nm(self):
        self.assertEqual(symbols(" U abs\n U system\n"), {"abs","system"})
        with self.assertRaises(ValueError):symbols("000 U abs")
        with self.assertRaises(ValueError):symbols(" U abs\n U abs")
    def test_removed_exactly_six(self):
        baseline=EXPECTED|SENSITIVE|{"unknown"}
        result=verify(baseline,SENSITIVE|{"unknown"})
        self.assertEqual(result["baseline_unresolved"]-result["after_pure_crt_unresolved"],6)
    def test_added_import_refused(self):
        with self.assertRaises(ValueError):verify(EXPECTED|SENSITIVE,SENSITIVE|{"evil"})
    def test_file_exec_not_allowed_to_disappear(self):
        with self.assertRaises(ValueError):verify(EXPECTED|SENSITIVE,SENSITIVE-{"system"})
    def test_expected_import_absent_refused(self):
        with self.assertRaises(ValueError):verify((EXPECTED-{"abs"})|SENSITIVE,SENSITIVE)
if __name__=="__main__":
    unittest.main()
