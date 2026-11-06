import unittest
from inventory import object_list, nm_symbols

class InventoryTests(unittest.TestCase):
    def test_required_engine_and_excluded_platform(self):
        src = "SRC_DOOM = doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o doomgeneric_soso.o"
        self.assertEqual(object_list(src), ["doomgeneric.c", "d_main.c", "g_game.c", "w_wad.c", "z_zone.c"])
    def test_no_source_list(self):
        with self.assertRaises(ValueError): object_list("SOMETHING = anything")
    def test_duplicate(self):
        with self.assertRaises(ValueError): object_list("SRC_DOOM = doomgeneric.o doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o doomgeneric_soso.o")
    def test_bad_filename(self):
        with self.assertRaises(ValueError): object_list("SRC_DOOM = doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o ../bad.o doomgeneric_soso.o")
    def test_unexpected_platform_change(self):
        with self.assertRaises(ValueError): object_list("SRC_DOOM = doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o")
    def test_nm_symbols(self):
        a,b=nm_symbols("0000000000000000 T known\n                 U malloc\n0000000000000000 D thing\n")
        self.assertEqual(a, {"known", "thing"})
        self.assertEqual(b, {"malloc"})

if __name__ == "__main__":
    unittest.main()
