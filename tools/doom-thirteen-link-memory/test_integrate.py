import importlib.util
import pathlib
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("integrator", ROOT / "integrate.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Contract(unittest.TestCase):
    def setUp(self):
        self.owners = {sym: label for label, values in m.OWNERS.items() for sym in values}
        self.old = m.TARGET | m.PROTECTED | {"fprintf", "stderr"}

    def test_expected_thirteen(self):
        result = m.validate(self.old, self.old - m.TARGET, self.owners)
        self.assertEqual(result["original_unresolved"] - result["remaining_unresolved"], 13)

    def test_masked_host_function_rejected(self):
        with self.assertRaisesRegex(ValueError, "protected"):
            m.validate(self.old, self.old - m.TARGET - {"system"}, self.owners)

    def test_new_external_rejected(self):
        with self.assertRaisesRegex(ValueError, "delta"):
            m.validate(self.old, (self.old - m.TARGET) | {"untrusted_new"}, self.owners)

    def test_missing_provider_rejected(self):
        owners = dict(self.owners)
        owners["strtol"] = "wrong"
        with self.assertRaisesRegex(ValueError, "ownership"):
            m.validate(self.old, self.old - m.TARGET, owners)

    def test_fake_success_rejected(self):
        with self.assertRaisesRegex(ValueError, "baseline"):
            m.validate(self.old - {"abs"}, self.old - m.TARGET, self.owners)

    def test_elf64_sections_and_bad_files(self):
        with tempfile.TemporaryDirectory() as td:
            file = pathlib.Path(td) / "test.o"
            data = bytearray(64 + 3*64 + 8)
            data[:7] = b"\\x7fELF\\x02\\x01\\x01"
            struct.pack_into("<HH", data, 16, 1, 62)
            struct.pack_into("<Q", data, 40, 64)
            struct.pack_into("<HH", data, 58, 64, 3)
            # data section index 1: 8 file-backed bytes.
            struct.pack_into("<IIQQQQIIQQ", data, 64+64, 0, 1, 2, 0, 256, 8, 0, 0, 8, 0)
            # BSS index 2: 4096 bytes, no bytes stored in file.
            struct.pack_into("<IIQQQQIIQQ", data, 64+128, 0, 8, 3, 0, 0, 4096, 0, 0, 16, 0)
            file.write_bytes(data)
            s=m.section_footprint(file)
            self.assertEqual((s["allocatable_total_section_bytes"],s["allocatable_file_backed_bytes"],s["allocatable_nobits_bytes"]), (4104,8,4096))
            data[4] = 1
            file.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "ELF64"):
                m.section_footprint(file)


if __name__ == "__main__":
    unittest.main()
