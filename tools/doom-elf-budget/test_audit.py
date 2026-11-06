import unittest
from audit import parse_readelf, validate

HEADER = """Elf file type is EXEC (Executable file)
Entry point 0x800000
Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  LOAD           0x001000 0x0000000000800000 0x0000000000800000 0x002000 0x002000 R E 0x1000
  LOAD           0x003000 0x0000000000840000 0x0000000000840000 0x001000 0x070000 RW  0x1000
"""

class AuditTests(unittest.TestCase):
    def parse(self, text=HEADER):
        return parse_readelf(text)

    def check(self, text=HEADER):
        return validate(*self.parse(text))

    def test_valid_two_segments(self):
        self.assertEqual(self.check(), [])

    def test_entry_outside_exec(self):
        self.assertIn('entry point', ' '.join(self.check(HEADER.replace('Entry point 0x800000', 'Entry point 0x840000'))))

    def test_code_overflow(self):
        self.assertIn('outside code window', ' '.join(self.check(HEADER.replace('0x002000 R E', '0x050000 R E'))))

    def test_heap_overflow(self):
        self.assertIn('outside data window', ' '.join(self.check(HEADER.replace('0x070000 RW', '0x1b0000 RW'))))

    def test_wx(self):
        self.assertIn('W^X', ' '.join(self.check(HEADER.replace('R E 0x1000', 'RWE 0x1000'))))

    def test_overlap(self):
        self.assertIn('overlapping', ' '.join(self.check(HEADER.replace('0x0000000000840000', '0x0000000000801000'))))

    def test_interp_rejected(self):
        self.assertIn('unsupported', ' '.join(self.check(HEADER + '  INTERP 0x001000 0x800000 0x800000 0x20 0x20 R 0x1\n')))

    def test_empty(self):
        with self.assertRaises(ValueError):
            self.parse('')

    def test_segment_without_load(self):
        self.assertIn('no PT_LOAD', ' '.join(self.check('Entry point 0x800000\n')))

    def test_zero_mem(self):
        self.assertIn('empty/invalid', ' '.join(self.check(HEADER.replace('0x002000 R E', '0x000000 R E'))))

    def test_filesz_gt_mem(self):
        self.assertIn('empty/invalid', ' '.join(self.check(HEADER.replace('0x001000 0x070000 RW', '0x080000 0x070000 RW'))))

if __name__ == '__main__':
    unittest.main()
