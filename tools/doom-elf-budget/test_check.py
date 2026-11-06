import struct
import unittest
from check import ELF, PH, inspect, Invalid

def fixture(*, mem=8192, filesz=8, flags=5, entry=0x800000, va=0x800000, phoff=64, dynamic=False):
    b = bytearray(4096)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    ELF.pack_into(b, 0, ident, 2, 62, 1, entry, phoff, 0, 0, 64, 56, 2 if dynamic else 1, 0, 0, 0)
    PH.pack_into(b, phoff, 1, flags, 0x1000, va, 0, filesz, mem, 4096)
    if dynamic:
        PH.pack_into(b, phoff + 56, 2, 4, 0, 0, 0, 0, 0, 1)
    b.extend(bytes(0x1000))
    return bytes(b)

class TestELF(unittest.TestCase):
    def test_valid(self):
        x = inspect(fixture())
        self.assertEqual(x['mapped_pages'], 2)
        self.assertGreater(x['unallocated_window_bytes'], 0)
    def test_no_elf(self):
        with self.assertRaises(Invalid): inspect(b'')
    def test_wx(self):
        with self.assertRaisesRegex(Invalid, 'writable and executable'): inspect(fixture(flags=7))
    def test_dynamic(self):
        with self.assertRaisesRegex(Invalid, 'dynamic'): inspect(fixture(dynamic=True))
    def test_wrong_entry(self):
        with self.assertRaisesRegex(Invalid, 'entry'): inspect(fixture(entry=0x900000))
    def test_over_window(self):
        with self.assertRaisesRegex(Invalid, 'outside window'): inspect(fixture(mem=0x300000))
    def test_invalid_file(self):
        with self.assertRaisesRegex(Invalid, 'truncated'): inspect(fixture(filesz=9000))
    def test_budget(self):
        with self.assertRaisesRegex(Invalid, 'exceeds window'): inspect(fixture(mem=0x1f0000))
    def test_bad_window(self):
        with self.assertRaisesRegex(Invalid, 'window'): inspect(fixture(), hi=0x900001)
    def test_negative_reserve(self):
        with self.assertRaisesRegex(Invalid, 'negative'): inspect(fixture(), stack=-1)
    def test_short_program_table(self):
        bad = bytearray(fixture())
        struct.pack_into('<Q', bad, 32, 0x3000)
        with self.assertRaisesRegex(Invalid, 'program header'): inspect(bytes(bad))

if __name__ == '__main__':
    unittest.main()
