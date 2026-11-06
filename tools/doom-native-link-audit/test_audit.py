import unittest
from audit import source_names, sections, unresolved, evaluate, NATIVE_WINDOW_BYTES

class AuditTests(unittest.TestCase):
    def test_manifest(self):
        names=[f"extra{i}.o" for i in range(70)]
        text="SRC_DOOM = doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o doomgeneric_soso.o "+" ".join(names)
        self.assertEqual(len(source_names(text)),75)
    def test_invalid_missing(self):
        with self.assertRaises(ValueError): source_names("OTHER = x")
    def test_bad_traversal(self):
        text="SRC_DOOM = doomgeneric.o d_main.o g_game.o w_wad.o z_zone.o doomgeneric_soso.o "+" ".join(f"x{i}.o" for i in range(70))+" ../bad.o"
        with self.assertRaises(ValueError): source_names(text)
    def test_sections_alloc_only(self):
        data="  [ 1] .text PROGBITS 0000000000000000 000040 000013 00 AX 0 0 1\n  [ 2] .bss NOBITS 0000000000000000 000058 000004 00 WA 0 0 4\n  [ 3] .comment PROGBITS 0000000000000000 000058 000020 01 MS 0 0 1"
        s=sections(data)
        self.assertEqual(len(s),2)
        q=evaluate(s,["malloc"])
        self.assertEqual(q["allocatable_section_bytes"],23)
        self.assertEqual(q["allocatable_zero_fill_bytes"],4)
    def test_no_executable(self):
        with self.assertRaises(ValueError): sections("  [ 1] .bss NOBITS 0000000000000000 000040 000100 00 WA 0 0 8")
    def test_undefined(self):
        self.assertEqual(unresolved("malloc U\nmemcpy U\nmemcpy U\nreal T 000000"),["malloc","memcpy"])
    def test_wide_section_budget(self):
        s=[{"name":".text","type":"PROGBITS","size_bytes":NATIVE_WINDOW_BYTES+1,"writable":False,"executable":True}]
        self.assertTrue(evaluate(s,[])["raw_section_sum_exceeds_native_pong_window"])

if __name__ == "__main__":
    unittest.main()
