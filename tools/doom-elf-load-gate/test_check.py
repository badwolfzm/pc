import struct
import unittest
from check import HEADER, PHDR, MIB, inspect

def elf(*, memsz=8*MIB, filesz=4, entry=0x400000, flags=5,
        ptype=1, align=4096, va=0x400000, off=4096):
    b = bytearray(off + filesz)
    ident = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9)
    HEADER.pack_into(b, 0, ident, 2, 62, 1, entry, 64, 0, 0,
                     HEADER.size, PHDR.size, 1, 0, 0, 0)
    PHDR.pack_into(b, 64, ptype, flags, off, va, 0, filesz, memsz, align)
    return bytes(b)

class ElfGateTests(unittest.TestCase):
    def test_accept(self):
        result = inspect(elf())
        self.assertEqual(result['segments'], 1)
        self.assertEqual(result['segment_pages_bytes'], 8*MIB)

    def test_reject(self):
        bad = [
            elf()[:20],
            elf()[:-1],
            elf(memsz=513*MIB),
            elf(filesz=9, memsz=4),
            elf(entry=0x5000000),
            elf(flags=7),
            elf(ptype=3),
            elf(align=3),
            elf(va=0x400001),
            elf(va=0x800000000000),
            elf(memsz=0),
        ]
        for blob in bad:
            with self.subTest(size=len(blob)), self.assertRaises(ValueError):
                inspect(blob)
        with self.assertRaises(ValueError):
            inspect(elf(), guest_bytes=4*MIB)
        with self.assertRaises(ValueError):
            inspect(elf(), stack_bytes=513*MIB)

if __name__ == '__main__':
    unittest.main()
