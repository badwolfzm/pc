import pathlib
import tempfile
import subprocess
import unittest
from audit_combined import inspect, DANGEROUS_HOST_IMPORTS

class CombinedTests(unittest.TestCase):
    def test_genuine_et_rel_inspection(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            source=root/"test.c"
            source.write_text("int pending(int); int buffers[120]; int run(void) { return pending(buffers[0]); }\n")
            output=root/"test.o"
            subprocess.run(["cc","-ffreestanding","-fno-builtin","-fno-pic","-fno-pie","-c",str(source),"-o",str(output)],check=True)
            report=inspect(output)
            self.assertIn("pending",report["upstream_engine_unresolved_symbols"])
            self.assertGreater(report["allocatable_zero_fill_bytes"],0)
            self.assertGreater(report["executable_section_bytes"],0)
            self.assertFalse(report["native_warden_executable_built"])
            self.assertEqual(report["host_privilege_imports_require_policy"], [])
            self.assertFalse(report["host_privilege_imports_policy_approved"])
    def test_explicit_host_authority_is_not_silent(self):
        self.assertTrue({"system", "fopen", "rename", "mkdir"}.issubset(DANGEROUS_HOST_IMPORTS))
        self.assertNotIn("strlen", DANGEROUS_HOST_IMPORTS)
    def test_deny_missing(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):
                inspect(pathlib.Path(d)/"not-present.o")
    def test_deny_non_elf(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/"text"
            p.write_text("not elf")
            with self.assertRaises(subprocess.CalledProcessError):
                inspect(p)
if __name__=="__main__":
    unittest.main()
