import importlib.util
import pathlib
import struct
import unittest

spec = importlib.util.spec_from_file_location("preflight", pathlib.Path(__file__).with_name("preflight.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def elf(*, typ=2, machine=62, segflags=5, vaddr=0x400000, entry=0x400000,
        filesz=1, memsz=4096, offset=0x1000, ph_type=1,
        align=0x1000, second=None):
    h = struct.Struct("<16sHHIQQQIHHHHHH")
    ph = struct.Struct("<IIQQQQQQ")
    ident = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
    count = 1 + (second is not None)
    data = bytearray(max(0x2000, offset + max(filesz, 0)))
    h.pack_into(data, 0, ident, typ, machine, 1, entry, h.size, 0, 0, h.size, ph.size, count, 0, 0, 0)
    ph.pack_into(data, h.size, ph_type, segflags, offset, vaddr, vaddr, filesz, memsz, align)
    if second is not None:
        ph.pack_into(data, h.size + ph.size, *second)
    return bytes(data)

class PreflightTests(unittest.TestCase):
    def ok(self, data, reserved=100):
        return module.inspect(data, 512*1048576, reserved*1048576)

    def bad(self, data, reserved=100):
        with self.assertRaises(module.InvalidELF):
            self.ok(data, reserved)

    def test_valid_static_exec(self):
        report = self.ok(elf())
        self.assertEqual(report["load_page_rounded_bytes"], 4096)
        self.assertFalse(report["guest_boot_verified"])

    def test_truncated(self):
        self.bad(b"\x7fELF")
    def test_deny_rel(self):
        self.bad(elf(typ=1))
    def test_deny_dynamic(self):
        self.bad(elf(typ=3))
    def test_deny_wrong_machine(self):
        self.bad(elf(machine=183))
    def test_deny_wx(self):
        self.bad(elf(segflags=7))
    def test_entry_not_executable(self):
        self.bad(elf(segflags=4))
    def test_file_larger_than_memory(self):
        self.bad(elf(filesz=2, memsz=1))
    def test_virtual_overflow(self):
        self.bad(elf(vaddr=(1 << 64) - 4096, entry=(1 << 64) - 4096, align=1, memsz=8192))
    def test_null_page(self):
        self.bad(elf(vaddr=0, entry=0, align=1))
    def test_alignment(self):
        self.bad(elf(align=3))
    def test_segment_past_eof(self):
        self.bad(elf(filesz=0x3000)[:0x2000])
    def test_no_interp(self):
        self.bad(elf(ph_type=3))
    def test_no_dynamic_segment(self):
        self.bad(elf(ph_type=2))
    def test_executable_stack_rejected(self):
        self.bad(elf(second=(0x6474e551, 7, 0, 0, 0, 0, 0, 0)))
    def test_overlap_rejected(self):
        self.bad(elf(second=(1, 4, 0x1000, 0x400800, 0x400800, 1, 4096, 1)))
    def test_insufficient_budget(self):
        self.bad(elf(memsz=420*1048576, align=1), reserved=100)
    def test_invalid_reserve(self):
        with self.assertRaises(module.InvalidELF):
            module.inspect(elf(), 1000, 1000)
    def test_noncanonical(self):
        self.bad(elf(vaddr=1 << 47, entry=1 << 47, align=1))
    def test_phdr_truncated(self):
        self.bad(elf()[:80])
    def test_two_loads(self):
        second = (1, 6, 0x1000, 0x600000, 0x600000, 1, 4096, 0x1000)
        r = self.ok(elf(second=second))
        self.assertEqual(r["load_segments"], 2)

if __name__ == "__main__":
    unittest.main()
