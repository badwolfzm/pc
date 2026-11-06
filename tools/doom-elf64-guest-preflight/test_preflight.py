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

    def test_strict_guest_ram_limit(self):
        for mib in (513, 1024, 4096):
            with self.subTest(mib=mib), self.assertRaisesRegex(module.InvalidELF, "512 MiB"):
                module.inspect(elf(), mib * 1048576, 0)

    def test_exact_512_mib_boundary(self):
        report = module.inspect(elf(), 512 * 1048576, 0)
        self.assertEqual(report["guest_ram_bytes_configured"], 512 * 1048576)

    def test_boolean_budgets_fail_closed(self):
        for guest, reserved in ((True, 0), (512 * 1048576, False),
                                (False, 0), (512 * 1048576, True)):
            with self.subTest(guest=guest, reserved=reserved), self.assertRaises(module.InvalidELF):
                module.inspect(elf(), guest, reserved)

    def test_invalid_guest_ram_limits(self):
        for guest in (0, -1, 512 * 1048576 + 1):
            with self.subTest(guest=guest), self.assertRaises(module.InvalidELF):
                module.inspect(elf(), guest, 0)

    def test_reserved_budget_must_leave_room(self):
        with self.assertRaises(module.InvalidELF):
            module.inspect(elf(), 512 * 1048576, 512 * 1048576)
        with self.assertRaises(module.InvalidELF):
            module.inspect(elf(), 512 * 1048576, -1)

    def test_cli_rejects_oversized_guest(self):
        import tempfile
        from contextlib import redirect_stdout
        from io import StringIO
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "valid.elf"
            path.write_bytes(elf())
            output = StringIO()
            with redirect_stdout(output):
                code = module.main([str(path), "--guest-mib", "1024", "--reserved-mib", "0"])
            self.assertEqual(code, 1)
            self.assertIn('"valid": false', output.getvalue().lower())

    def test_entry_in_zero_filled_executable_tail_rejected(self):
        # p_memsz is larger than p_filesz. The entry points beyond source bytes.
        self.bad(elf(filesz=1, memsz=4096, entry=0x400001))

    def test_entry_within_file_backed_executable_bytes_accepted(self):
        report = self.ok(elf(filesz=3, memsz=4096, entry=0x400002))
        self.assertEqual(report["entry"], 0x400002)

    def test_zero_file_size_executable_rejected(self):
        self.bad(elf(filesz=0, memsz=4096))

    def test_entry_at_file_backed_end_rejected(self):
        self.bad(elf(filesz=2, memsz=4096, entry=0x400002))

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
    def test_page_alias_rx_and_read_only_rejected(self):
        # Disjoint byte ranges share one 4 KiB page, but the first wants RX
        # and the second read-only; a single mapping cannot satisfy both.
        second = (1, 4, 0x1800, 0x400800, 0x400800, 1, 0x800, 1)
        self.bad(elf(memsz=0x800, second=second))

    def test_page_alias_read_only_and_writable_rejected(self):
        second = (1, 6, 0x1800, 0x400800, 0x400800, 1, 0x800, 1)
        self.bad(elf(segflags=4, memsz=0x800, second=second))

    def test_distinct_pages_with_distinct_permissions_allowed(self):
        second = (1, 6, 0x2000, 0x401000, 0x401000, 1, 0x800, 0x1000)
        report = self.ok(elf(memsz=0x800, second=second))
        self.assertEqual(report["load_segments"], 2)

    def test_page_alias_wx_rejected(self):
        # No byte overlap; executable and writable mappings alias one 4 KiB frame.
        second = (1, 6, 0x1800, 0x400800, 0x400800, 1, 0x800, 1)
        self.bad(elf(memsz=0x800, second=second))
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
