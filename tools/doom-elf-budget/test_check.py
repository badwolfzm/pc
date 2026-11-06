import unittest
from check import HEADER, PROGRAM, BudgetError, evaluate

BASE = 0x400000

def fixture(mem=8192, filesz=4, vaddr=BASE, offset=4096, flags=5, entry=BASE, align=4096):
    data = bytearray(max(4096 + filesz, HEADER.size + PROGRAM.size))
    ident = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
    HEADER.pack_into(data, 0, ident, 2, 62, 1, entry, HEADER.size, 0, 0,
                     HEADER.size, PROGRAM.size, 1, 0, 0, 0)
    PROGRAM.pack_into(data, HEADER.size, 1, flags, offset, vaddr, 0, filesz, mem, align)
    return bytes(data)

def check(data):
    return evaluate(data, BASE)

class BudgetTests(unittest.TestCase):
    def test_valid_budget(self):
        r = check(fixture())
        self.assertEqual(r["mapped_bytes"], 8192)
        self.assertEqual(r["remaining_bytes"], 2097152-8192-131072-262144)
    def test_non_elf(self):
        with self.assertRaises(BudgetError): check(b"no elf")
    def test_non_executable_segment(self):
        with self.assertRaisesRegex(BudgetError, "entry"): check(fixture(flags=4))
    def test_outside_window(self):
        with self.assertRaisesRegex(BudgetError, "outside"): check(fixture(vaddr=BASE+2097152))
    def test_invalid_size(self):
        with self.assertRaisesRegex(BudgetError, "size"): check(fixture(mem=1))
    def test_invalid_alignment(self):
        with self.assertRaisesRegex(BudgetError, "alignment"): check(fixture(align=3))
    def test_exhaust_budget(self):
        with self.assertRaisesRegex(BudgetError, "exceeded"): check(fixture(mem=1900*1024))
    def test_entry_outside_executable(self):
        with self.assertRaisesRegex(BudgetError, "entry"): check(fixture(entry=BASE+0x9000))
    def test_short_program_headers(self):
        with self.assertRaisesRegex(BudgetError, "truncated"): check(fixture()[:70])
    def test_misaligned_vaddr(self):
        with self.assertRaisesRegex(BudgetError, "alignment"): check(fixture(vaddr=BASE+1))

if __name__ == "__main__":
    unittest.main()
