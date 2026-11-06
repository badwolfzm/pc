import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

M = pathlib.Path(__file__).with_name("compose.py")
spec = importlib.util.spec_from_file_location("composed_native_doom", M)
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class ContractTests(unittest.TestCase):
    def test_expected_symbol_set_size_and_safety(self):
        self.assertEqual(len(bundle.EXPECTED), 16)
        self.assertTrue({"system", "fopen", "rename"}.issubset(bundle.PROTECTED))
        self.assertEqual(bundle.closure(bundle.EXPECTED | bundle.PROTECTED,
                                        bundle.PROTECTED), sorted(bundle.EXPECTED))

    def test_fail_closed_missing_target(self):
        with self.assertRaisesRegex(ValueError, "missing expected"):
            bundle.closure(bundle.EXPECTED - {"snprintf"} | bundle.PROTECTED,
                           bundle.PROTECTED)

    def test_fail_closed_extra_import(self):
        with self.assertRaisesRegex(ValueError, "exactly"):
            bundle.closure(bundle.EXPECTED | bundle.PROTECTED,
                           bundle.PROTECTED | {"__new_unsafe_helper"})

    def test_fail_closed_missing_protected_import(self):
        with self.assertRaisesRegex(ValueError, "protected"):
            bundle.closure(bundle.EXPECTED | bundle.PROTECTED,
                           bundle.PROTECTED - {"system"})

    def test_fail_closed_incomplete_resolution(self):
        with self.assertRaisesRegex(ValueError, "exactly"):
            bundle.closure(bundle.EXPECTED | bundle.PROTECTED,
                           bundle.PROTECTED | {"strtol"})

    def test_duplicate_baseline_and_overlapping_owners(self):
        with self.assertRaisesRegex(ValueError, "duplicate global provider"):
            bundle.ensure_unique({"malloc"}, [("heap", {"malloc"})])
        with self.assertRaisesRegex(ValueError, "duplicate global provider"):
            bundle.ensure_unique(set(), [("one", {"snprintf"}), ("two", {"snprintf"})])
        own = bundle.ensure_unique({"strlen"}, [("heap", {"malloc", "free"})])
        self.assertEqual(own["malloc"], "heap")
        self.assertEqual(own["strlen"], "base")

    def test_nm_export_parser_with_live_elf_objects(self):
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            p = d / "provider.c"
            o = d / "provider.o"
            p.write_text("int my_export(int x) { return x + 1; }\n")
            subprocess.run(["cc", "-ffreestanding", "-fno-builtin",
                            "-c", str(p), "-o", str(o)], check=True)
            defined, undefined = bundle.exports(o)
            self.assertEqual(defined, {"my_export"})
            self.assertEqual(undefined, set())


if __name__ == "__main__":
    unittest.main()
