import importlib.util
from pathlib import Path
import unittest

p = Path(__file__).with_name("compose17.py")
spec = importlib.util.spec_from_file_location("compose17", p)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

BASE = {"__isoc99_sscanf", "system", "fopen", "fprintf", "fread", "remaining"}
SCANNER = {"__isoc99_sscanf"}


class CompositionTests(unittest.TestCase):
    def test_one_true_symbol_closure(self):
        result = m.check(BASE, BASE - SCANNER, set(), SCANNER, set())
        self.assertEqual(result["resolved_only"], ["__isoc99_sscanf"])
        self.assertEqual(result["after_missing"], len(BASE) - 1)
        self.assertFalse(result["native_warden_boot_verified"])

    def test_no_missing_original_import(self):
        with self.assertRaises(ValueError):
            m.check(BASE - SCANNER, BASE - SCANNER, set(), SCANNER, set())

    def test_no_extra_missing_import(self):
        with self.assertRaises(ValueError):
            m.check(BASE, (BASE - SCANNER) | {"danger"}, set(), SCANNER, set())

    def test_no_hidden_host_io_closure(self):
        with self.assertRaises(ValueError):
            m.check(BASE, BASE - SCANNER - {"fopen"}, set(), SCANNER, set())

    def test_no_duplicate_provider(self):
        with self.assertRaises(ValueError):
            m.check(BASE, BASE - SCANNER, set(), SCANNER, SCANNER)

    def test_no_unresolved_scanner_import(self):
        with self.assertRaises(ValueError):
            m.check(BASE, BASE - SCANNER, {"malloc"}, SCANNER, set())

    def test_only_expected_symbol_export(self):
        with self.assertRaises(ValueError):
            m.check(BASE, BASE - SCANNER, set(), SCANNER | {"system"}, set())

    def test_no_fake_complete_link(self):
        result = m.check(BASE, BASE - SCANNER, set(), SCANNER, set())
        self.assertEqual(result["output_kind"], "ET_REL genuine Doom partial link only")
        self.assertFalse(result["final_executable_built"])
        self.assertFalse(result["guest_512_mib_measured"])


if __name__ == "__main__":
    unittest.main()
