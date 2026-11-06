import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).with_name("audit.py")
spec = importlib.util.spec_from_file_location("audit", path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class AuditTests(unittest.TestCase):
    def test_comments_do_not_count(self):
        rows = m.scan_source('/* printf("%n"); */ // puts("x")\nprintf("%s", "ok");', "a.c")
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["problems"], [])
    def test_split_adjacent_literal(self):
        rows = m.scan_source('printf("value %d" " and %x", x, y);', "a.c")
        self.assertEqual(rows[0]["classification"], "literal")
        self.assertEqual(rows[0]["problems"], [])
    def test_unsafe_n_and_float(self):
        rows = m.scan_source('printf("%n %.2f %p", p, f, q);', "a.c")
        self.assertEqual(set(rows[0]["problems"]), {"unsupported-n", "unsupported-f", "unsupported-p"})
    def test_dynamic_and_nested_args(self):
        rows = m.scan_source('snprintf(dst, size(a,b), fmt, 4);', "a.c")
        self.assertEqual(rows[0]["classification"], "dynamic-or-unparsed")
    def test_literal_precision_and_percent(self):
        rows = m.scan_source('fprintf(stderr, "%% %-5.2s %05d", s, i);', "a.c")
        self.assertEqual(rows[0]["problems"], ["unimplemented-stdio-function"])
    def test_trailing_percent(self):
        rows = m.scan_source('printf("bad%");', "a.c")
        self.assertIn("malformed-percent", rows[0]["problems"])
    def test_star_width_explicitly_unverified(self):
        rows = m.scan_source('printf("%*d", width, value);', "a.c")
        self.assertIn("dynamic-width-or-precision", rows[0]["problems"])
    def test_missing_fprintf_runtime_flagged_even_with_supported_format(self):
        rows = m.scan_source('fprintf(stderr, "%d", 3);', "a.c")
        self.assertIn("unimplemented-stdio-function", rows[0]["problems"])
        self.assertNotIn("unsupported-d", rows[0]["problems"])
    def test_merged_printf_runtime_not_flagged_for_supported_format(self):
        rows = m.scan_source('printf("%d", 3);', "a.c")
        self.assertEqual(rows[0]["problems"], [])
    def test_dynamic_vfprintf_reports_missing_abi_and_dynamic_expression(self):
        rows = m.scan_source('vfprintf(stderr, fmt, ap);', "a.c")
        self.assertEqual(set(rows[0]["problems"]), {"unimplemented-stdio-function", "manual-review-required"})
    def test_nested_nested_string_not_literal(self):
        rows = m.scan_source('printf((format), 10);', "a.c")
        self.assertEqual(rows[0]["classification"], "dynamic-or-unparsed")

if __name__ == "__main__":
    unittest.main()
