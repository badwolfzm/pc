import unittest
from probe import source_names, parse_undefined

SAMPLE = "SRC_DOOM = doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o doomgeneric_soso.o"

class ProbeTests(unittest.TestCase):
    def test_extract(self):
        self.assertEqual(source_names(SAMPLE), ["doomgeneric.c", "d_main.c", "g_game.c", "w_wad.c", "z_zone.c"])
    def test_missing(self):
        with self.assertRaises(ValueError): source_names("missing")
    def test_backend_missing(self):
        with self.assertRaises(ValueError): source_names(SAMPLE.replace("doomgeneric_soso.o", "other.o"))
    def test_duplicate(self):
        with self.assertRaises(ValueError): source_names(SAMPLE + " d_main.o")
    def test_path_traversal(self):
        with self.assertRaises(ValueError): source_names(SAMPLE + " ../escape.o")
    def test_undefined(self):
        self.assertEqual(parse_undefined("malloc U\nmemcpy U\nmemcpy U\nfn T 0\n"), ["malloc", "memcpy"])
    def test_blank_undefined(self):
        self.assertEqual(parse_undefined("some.o:\nfoo T 0\n"), [])

if __name__ == "__main__":
    unittest.main()
