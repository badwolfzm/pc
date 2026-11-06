import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location("check",Path(__file__).with_name("check.py"))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Closure(unittest.TestCase):
    def test_proven(self):
        self.assertEqual(m.verify(["strdup","fopen","system"],["fopen","system"]),["strdup"])
    def test_missing_start(self):
        with self.assertRaises(ValueError):m.verify(["malloc"],[])
    def test_still_missing(self):
        with self.assertRaises(ValueError):m.verify(["strdup"],["strdup"])
    def test_new_import(self):
        with self.assertRaises(ValueError):m.verify(["strdup"],["kernel_escape"])
    def test_unsafe_suppression(self):
        with self.assertRaises(ValueError):m.verify(["strdup","system"],[])
if __name__=="__main__":unittest.main()
