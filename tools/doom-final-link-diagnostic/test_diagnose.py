import importlib.util
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("diag", pathlib.Path(__file__).with_name("diagnose.py"))
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)

class LinkTests(unittest.TestCase):
    def scenario(self, missing=None, defined=None, returncode=1, stderr="ld: test.o: undefined reference to `fopen'"):
        with tempfile.TemporaryDirectory() as tmp:
            base = pathlib.Path(tmp)
            source, out, report = base/"candidate.o", base/"final.elf", base/"report.json"
            source.write_bytes(b"fake-object")
            imports = set(d.REQUIRED_HOST_IMPORTS) if missing is None else missing
            exports = {d.ENTRY} if defined is None else defined
            with patch.object(d, "nm", side_effect=lambda p, undefined=False: imports if undefined else exports):
                with patch.object(d.subprocess, "run", return_value=subprocess.CompletedProcess([], returncode, "", stderr)) as linker:
                    result = d.diagnose(source, out, report)
                    self.assertTrue(report.is_file())
                    self.assertFalse(result["final_executable_built"])
                    self.assertEqual(result["unresolved_import_count"], len(imports))
                    self.assertIn("--no-undefined", linker.call_args.args[0])

    def test_expected_negative_link(self):
        self.scenario()
    def test_no_entry_rejected(self):
        with self.assertRaises(d.DiagnosticError): self.scenario(defined={"unrelated"})
    def test_missing_protected_rejected(self):
        with self.assertRaises(d.DiagnosticError): self.scenario(missing={"fopen"})
    def test_link_success_rejected(self):
        with self.assertRaises(d.DiagnosticError): self.scenario(returncode=0)
    def test_non_symbol_failure_rejected(self):
        with self.assertRaises(d.DiagnosticError): self.scenario(stderr="ld: invalid option")
    def test_hash_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/"candidate.o"
            p.write_bytes(b"fake")
            with self.assertRaises(d.DiagnosticError):
                d.diagnose(p,pathlib.Path(tmp)/"out",pathlib.Path(tmp)/"report", "0"*64)

if __name__=="__main__":
    unittest.main()
