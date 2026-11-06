import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).with_name("triage.py")
spec = importlib.util.spec_from_file_location("triage", SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"


def report():
    return {"native_executable_built": False, "native_warden_boot_verified": False,
            "upstream_commit": PIN, "unresolved_symbol_count": 3,
            "unresolved_symbols": ["sqrt", "fopen", "__errno_location"],
            "unresolved_symbol_provenance": {
                "sqrt": ["a.o"], "fopen": ["a.o", "b.o"], "__errno_location": ["c.o"]}}


class TriageTests(unittest.TestCase):
    def test_order_and_no_boot_claim(self):
        result = mod.triage(report())
        self.assertEqual([r["symbol"] for r in result["ranked_imports"]],
                         ["fopen", "__errno_location", "sqrt"])
        self.assertFalse(result["native_warden_boot_verified"])
        self.assertFalse(result["game_fps_measured"])

    def test_determinism(self):
        r = report()
        r["unresolved_symbols"].reverse()
        self.assertEqual(mod.triage(r), mod.triage(report()))

    def test_duplicate_symbols_rejected(self):
        r = report()
        r["unresolved_symbols"][0] = "fopen"
        with self.assertRaises(ValueError):
            mod.triage(r)

    def test_missing_provenance_rejected(self):
        r = report()
        del r["unresolved_symbol_provenance"]["sqrt"]
        with self.assertRaises(ValueError):
            mod.triage(r)

    def test_forged_boot_claim_rejected(self):
        r = report()
        r["native_warden_boot_verified"] = True
        with self.assertRaises(ValueError):
            mod.triage(r)

    def test_forged_counts_rejected(self):
        r = report()
        r["unresolved_symbol_count"] = 2
        with self.assertRaises(ValueError):
            mod.triage(r)

    def test_empty_and_unclassified(self):
        r = report()
        r["unresolved_symbols"] = ["unrecognized"]
        r["unresolved_symbol_count"] = 1
        r["unresolved_symbol_provenance"] = {"unrecognized": ["x.o"]}
        self.assertEqual(mod.triage(r)["ranked_imports"][0]["category"], "unclassified")

    def test_sensitive_host_libc_imports_are_not_misclassified(self):
        dangerous = ["fopen64", "fgets", "snprintf", "vfprintf", "openat",
                     "mprotect", "dlopen", "posix_spawn", "sendto",
                     "__isoc99_sscanf", "__isoc23_strtol", "__memcpy_chk"]
        for name in dangerous:
            with self.subTest(symbol=name):
                category, action = mod.classify(name)
                self.assertEqual(category, "privileged_host_boundary")
                self.assertIn("capability", action)

    def test_pure_runtime_remains_distinct(self):
        self.assertEqual(mod.classify("sqrt")[0], "numerical_runtime")
        self.assertEqual(mod.classify("malloc")[0], "bounded_memory")
        self.assertEqual(mod.classify("__errno_location")[0], "guest_runtime_state")
        self.assertEqual(mod.classify("unknown_custom_symbol")[0], "unclassified")

    def test_reject_path_traversal_in_source_name(self):
        r = report()
        r["unresolved_symbol_provenance"]["sqrt"] = ["../../x.o"]
        with self.assertRaises(ValueError):
            mod.triage(r)


if __name__ == "__main__":
    unittest.main()
