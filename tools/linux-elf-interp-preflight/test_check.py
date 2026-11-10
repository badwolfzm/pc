import struct
import unittest
from check import inspect, InvalidELF

def fixture(interp=None):
    b = bytearray(512)
    b[:16] = b'\x7fELF\x02\x01\x01' + bytes(9)
    struct.pack_into('<HHIQQQ', b, 16, 2, 62, 1, 0x400000, 64, 0)
    struct.pack_into('<HHH', b, 52, 64, 56, 2 if interp is not None else 1)
    struct.pack_into('<IIQQQQQQ', b, 64, 1, 5, 256, 0x400000, 0, 4, 4, 1)
    if interp is not None:
        raw = interp.encode() + b'\0'
        struct.pack_into('<IIQQQQQQ', b, 120, 3, 4, 300, 0, 0, len(raw), len(raw), 1)
        b[300:300+len(raw)] = raw
    return b

class Tests(unittest.TestCase):
    def test_static(self): self.assertIsNone(inspect(fixture())['interpreter'])
    def test_dynamic(self): self.assertEqual(inspect(fixture('/lib/ld-linux-x86-64.so.2'))['interpreter'], '/lib/ld-linux-x86-64.so.2')
    def test_traversal(self):
        with self.assertRaises(InvalidELF): inspect(fixture('/lib/../tmp/ld.so'))
    def test_relative(self):
        with self.assertRaises(InvalidELF): inspect(fixture('ld.so'))
    def test_header_oob(self):
        b=fixture(); struct.pack_into('<Q',b,32,500)
        with self.assertRaises(InvalidELF): inspect(b)
    def test_wx(self):
        b=fixture(); struct.pack_into('<I',b,68,7)
        with self.assertRaises(InvalidELF): inspect(b)
    def test_duplicate_interp(self):
        b=fixture('/lib/ld.so'); struct.pack_into('<H',b,56,3); b[176:232]=b[120:176]
        with self.assertRaises(InvalidELF): inspect(b)
    def test_bad_nul(self):
        b=fixture('/lib/ld.so'); b[300+len('/lib/ld.so')]=65
        with self.assertRaises(InvalidELF): inspect(b)
    def test_segment_oob(self):
        b=fixture(); struct.pack_into('<Q',b,96,9999)
        with self.assertRaises(InvalidELF): inspect(b)

if __name__ == '__main__': unittest.main()
