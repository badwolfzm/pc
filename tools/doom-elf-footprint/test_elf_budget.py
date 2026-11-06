import struct
import unittest
from elf_budget import ELFError, inspect

def elf(loads=None, entry=0x1000, typ=2):
    if loads is None:
        loads = [(0x1000, 16, 0x200, 5)]
    blob = bytearray(max([0x200] + [0x100 + i*0x100 + fs for i,(_,fs,_,_) in enumerate(loads)]))
    blob[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", blob, 16, typ, 62, 1, entry, 64, 0, 0, 64, 56, len(loads), 0, 0, 0)
    for i,(va,fs,ms,flags) in enumerate(loads):
        struct.pack_into("<IIQQQQQQ", blob, 64+i*56, 1, flags, 0x100+i*0x100, va, 0, fs, ms, 1)
    return bytes(blob)

class BudgetTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(inspect(elf(), 2*1024*1024)["load_span_bytes"], 0x200)
    def test_budget(self):
        with self.assertRaises(ELFError): inspect(elf([(0x1000,16,0x300000,5)]), 2*1024*1024)
    def test_short(self):
        with self.assertRaises(ELFError): inspect(b"not ELF", 2*1024*1024)
    def test_bad_phoff(self):
        x=bytearray(elf()); struct.pack_into("<Q",x,32,0xffffffffffffffff)
        with self.assertRaises(ELFError): inspect(x,2*1024*1024)
    def test_filesz_gt_memsz(self):
        with self.assertRaises(ELFError): inspect(elf([(0x1000,0x200,0x100,5)]),2*1024*1024)
    def test_wrong_entry(self):
        with self.assertRaises(ELFError): inspect(elf(entry=0x9999),2*1024*1024)
    def test_address_overflow(self):
        with self.assertRaises(ELFError): inspect(elf([(2**64-0x100,16,0x200,5)],entry=2**64-0x100),2*1024*1024)
    def test_overlap(self):
        with self.assertRaises(ELFError): inspect(elf([(0x1000,16,0x500,5),(0x1100,16,0x100,4)]),2*1024*1024)
    def test_et_dyn_classification(self):
        self.assertEqual(inspect(elf(typ=3),2*1024*1024)["type"],"ET_DYN")
    def test_entry_not_executable(self):
        with self.assertRaises(ELFError): inspect(elf([(0x1000,16,0x200,4)]),2*1024*1024)

if __name__ == "__main__": unittest.main()
