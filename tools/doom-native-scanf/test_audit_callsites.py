import importlib.util
import pathlib
import tempfile
import unittest

PATH = pathlib.Path(__file__).with_name("audit_callsites.py")
spec = importlib.util.spec_from_file_location("audit_callsites", PATH)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class CallsiteAuditTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.source = pathlib.Path(self.dir.name)
        for name, formats in mod.EXPECTED.items():
            (self.source / name).write_text(
                "\n".join(f'int value = sscanf(source, "{fmt}", out);' for fmt in formats),
                encoding="utf-8")
        self.gate = {
            "upstream_commit": mod.PIN,
            "unresolved_symbol_provenance":
                {"__isoc99_sscanf": sorted(mod.EXPECTED_OBJECTS)}
        }

    def test_exact_six_formats_pass(self):
        actual = mod.audit(self.source, self.gate)
        self.assertEqual(sum(len(v) for v in actual.values()), 6)

    def test_unreviewed_format_rejected(self):
        p = self.source / "m_misc.c"
        p.write_text(p.read_text() + '\nsscanf(source, "%s", out);\n')
        with self.assertRaises(ValueError):
            mod.audit(self.source, self.gate)

    def test_missing_call_rejected(self):
        p = self.source / "m_misc.c"
        p.write_text('sscanf(source, " %d", out);')
        with self.assertRaises(ValueError):
            mod.audit(self.source, self.gate)

    def test_provenance_mismatch_rejected(self):
        self.gate["unresolved_symbol_provenance"]["__isoc99_sscanf"] = ["other.o"]
        with self.assertRaises(ValueError):
            mod.audit(self.source, self.gate)

    def test_wrong_upstream_commit_rejected(self):
        self.gate["upstream_commit"] = "0" * 40
        with self.assertRaises(ValueError):
            mod.audit(self.source, self.gate)

    def test_comments_do_not_add_fake_calls(self):
        p = self.source / "m_misc.c"
        p.write_text(p.read_text() + '\n/* sscanf(source, "%s", out); */\n')
        self.assertEqual(len(mod.audit(self.source, self.gate)["m_misc.c"]), 4)


if __name__ == "__main__":
    unittest.main()
