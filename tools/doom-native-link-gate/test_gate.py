import importlib.util
import pathlib
import unittest
from unittest.mock import patch

path = pathlib.Path(__file__).with_name("gate.py")
spec = importlib.util.spec_from_file_location("link_gate", path)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

class NativeLinkGateTests(unittest.TestCase):
    def test_parse_real_nm_undefined_output(self):
        symbols = gate.missing_symbols("                 U malloc\n U fprintf\n U DG_Init\n")
        self.assertEqual(symbols, ["DG_Init", "fprintf", "malloc"])

    def test_deduplicates_undefined(self):
        self.assertEqual(gate.missing_symbols(" U fopen\n U fopen\n"), ["fopen"])

    def test_classification_and_explicit_host_io(self):
        c = gate.classify(["fopen", "malloc", "sqrt", "WardenMissing"])
        self.assertEqual(c["host_file_process_network_calls"], ["fopen"])
        self.assertEqual(c["allocation_calls"], ["malloc"])
        self.assertEqual(c["math_calls"], ["sqrt"])
        self.assertEqual(c["other"], ["WardenMissing"])

    def test_unknown_nm_undefined_shape_fails_closed(self):
        with self.assertRaises(ValueError):
            gate.missing_symbols("00000000 U malformed\n")

    def test_object_level_unresolved_provenance(self):
        with patch.object(gate, "execute", side_effect=[" U malloc\n U DG_Init\n", " U fclose\n"]):
            result = gate.unresolved_by_source(pathlib.Path("/nonexistent"), [
                (None, "upstream_engine.o"), (None, "warden_bridge.o")
            ])
        self.assertEqual(result, {
            "upstream_engine.o": ["DG_Init", "malloc"],
            "warden_bridge.o": ["fclose"],
        })

    def test_fortify_disabled_for_freestanding(self):
        self.assertIn("-U_FORTIFY_SOURCE", gate.CFLAGS)
        self.assertIn("-D_FORTIFY_SOURCE=0", gate.CFLAGS)

    def test_pinned_revision_and_backends(self):
        self.assertEqual(len(gate.PIN), 40)
        self.assertEqual(gate.ENGINE_OMIT, {"doomgeneric_soso.o", "w_file_stdc.o"})
        self.assertIn("-ffreestanding", gate.CFLAGS)
        self.assertIn("-fno-builtin", gate.CFLAGS)

if __name__ == "__main__":
    unittest.main()
