import importlib.util
import pathlib
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("five_verify", pathlib.Path(__file__).with_name("verify.py"))
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)

EXPECTED = set(v.EXPECTED)
HOST = set(v.PROTECTED)

class FiveTests(unittest.TestCase):
    def setup_scenario(self):
        old = EXPECTED | HOST | {"fprintf"}
        new = old - EXPECTED
        defined = {
            "ctype": {"__ctype_toupper_loc": "T"},
            "errno": {"__errno_location": "T"},
            "strtol": {"strtol": "T"},
            "format": {"snprintf": "T", "vsnprintf": "T"},
        }
        imports = {"ctype": set(), "errno": set(), "strtol": {"__errno_location"}, "format": set()}
        return old, new, defined, imports

    def execute(self, old, new, defs, imports):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            before, after = root / "before.o", root / "after.o"
            providers = {label: root / (label + ".o") for label in defs}
            for obj in providers.values():
                obj.write_bytes(b"fixture")
            def mocked(path, options):
                if path == before: return {s:"U" for s in old}
                if path == after: return {s:"U" for s in new}
                for label, obj in providers.items():
                    if path == obj:
                        if "--defined-only" in options: return defs[label]
                        return {s:"U" for s in imports[label]}
                raise AssertionError(path)
            with patch.object(v, "symbols", side_effect=mocked):
                return v.verify(before, after, providers, root / "report.json")

    def test_exact_five_passes(self):
        old, new, defs, imports = self.setup_scenario()
        report = self.execute(old, new, defs, imports)
        self.assertEqual(report["original_undefined_count"] - report["remaining_undefined_count"], 5)
        self.assertFalse(report["native_warden_boot_verified"])

    def test_unexpected_new_import(self):
        old, new, defs, imports = self.setup_scenario()
        new.add("system_new")
        with self.assertRaises(ValueError): self.execute(old, new, defs, imports)

    def test_missing_import_not_present_in_base(self):
        old, new, defs, imports = self.setup_scenario()
        old.remove("strtol")
        with self.assertRaises(ValueError): self.execute(old, new, defs, imports)

    def test_protected_host_import_disappears(self):
        old, new, defs, imports = self.setup_scenario()
        new.remove("system")
        with self.assertRaises(ValueError): self.execute(old, new, defs, imports)

    def test_wrong_owner(self):
        old, new, defs, imports = self.setup_scenario()
        defs["format"].pop("snprintf")
        defs["strtol"]["snprintf"] = "T"
        with self.assertRaises(ValueError): self.execute(old, new, defs, imports)

    def test_hidden_host_dependency(self):
        old, new, defs, imports = self.setup_scenario()
        imports["format"].add("fopen")
        with self.assertRaises(ValueError): self.execute(old, new, defs, imports)

    def test_duplicate_provider_export(self):
        old, new, defs, imports = self.setup_scenario()
        defs["format"]["strtol"] = "T"
        with self.assertRaises(ValueError): self.execute(old, new, defs, imports)

if __name__ == "__main__":
    unittest.main()
