import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

MODULE = pathlib.Path(__file__).with_name("verify.py")
spec = importlib.util.spec_from_file_location("verify_module", MODULE)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class VerifyTests(unittest.TestCase):
    def test_symbol_closure_and_negative_cases(self):
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            def obj(name, source):
                c = d / (name + ".c")
                out = d / (name + ".o")
                c.write_text(source)
                subprocess.run(["cc", "-std=c11", "-ffreestanding", "-fno-builtin",
                                "-c", str(c), "-o", str(out)], check=True)
                return out
            # No definitions of protected names: those must stay unresolved.
            missing = """
                extern int __ctype_toupper_loc(void);
                extern int system(const char*);
                extern int fopen(void), fread(void), fwrite(void), remove(void), rename(void);
                volatile int sink;
                void call(void) {
                    sink = __ctype_toupper_loc() + system(0) + fopen()
                         + fread() + fwrite() + remove() + rename();
                }
            """
            before = obj("before", missing)
            provider = obj("provider", "int __ctype_toupper_loc(void) { return 1; }")
            after = d / "after.o"
            subprocess.run(["ld", "-r", "-o", str(after), str(before), str(provider)], check=True)
            result = m.verify(before, after, provider, d / "report.json")
            self.assertEqual(result["resolved"], [m.TARGET])
            with self.assertRaisesRegex(ValueError, "did not require"):
                m.verify(after, after, provider, d / "negative.json")
            malicious = obj("malicious", "int __ctype_toupper_loc(void){return 1;} int system(const char*p){(void)p;return 0;}")
            with self.assertRaisesRegex(ValueError, "exactly one"):
                m.verify(before, after, malicious, d / "bad.json")
            with self.assertRaisesRegex(ValueError, "unexpected additional"):
                m.verify(before, before, provider, d / "nochange.json")


if __name__ == "__main__":
    unittest.main()
