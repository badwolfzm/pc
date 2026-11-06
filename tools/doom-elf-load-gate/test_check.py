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

    def test_page_accounting_unaligned_start(self):
        # A PT_LOAD that starts one byte into a page and spans 4096 bytes
        # occupies TWO pages, not one.
        blob = elf(memsz=4096, entry=0x400001, va=0x400001, off=4097,
                   align=1)
        result = inspect(blob)
        self.assertEqual(result['segment_pages_bytes'], 8192)
        with self.assertRaises(ValueError):
            inspect(blob, guest_bytes=8192, stack_bytes=4096)

    def test_disjoint_byte_extents_share_page(self):
        # Page-sharing is prohibited even when segment bytes do not overlap.
        blob = bytearray(8196)
        ident = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9)
        HEADER.pack_into(blob, 0, ident, 2, 62, 1, 0x400000, 64, 0, 0,
                         HEADER.size, PHDR.size, 2, 0, 0, 0)
        PHDR.pack_into(blob, 64, 1, 5, 4096, 0x400000, 0, 4, 2048, 4096)
        PHDR.pack_into(blob, 64 + PHDR.size, 1, 6, 8192, 0x400800, 0, 4, 2048, 1)
        with self.assertRaisesRegex(ValueError, 'share a virtual page'):
            inspect(bytes(blob))

    def test_page_aligned_disjoint_segments(self):
        blob = bytearray(8196)
        ident = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9)
        HEADER.pack_into(blob, 0, ident, 2, 62, 1, 0x400000, 64, 0, 0,
                         HEADER.size, PHDR.size, 2, 0, 0, 0)
        PHDR.pack_into(blob, 64, 1, 5, 4096, 0x400000, 0, 4, 4096, 4096)
        PHDR.pack_into(blob, 64 + PHDR.size, 1, 6, 8192, 0x401000, 0, 4, 4096, 4096)
        self.assertEqual(inspect(bytes(blob))['segment_pages_bytes'], 8192)

if __name__ == '__main__':
    unittest.main()
