import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

path = pathlib.Path(__file__).with_name("verify.py")
spec = importlib.util.spec_from_file_location("native_doom_console_verify", path)
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class Tests(unittest.TestCase):
    def test_strict_closure_with_real_nm(self):
        with tempfile.TemporaryDirectory() as t:
            d = pathlib.Path(t)
            def build(name, body):
                s, obj = d / (name + ".c"), d / (name + ".o")
                s.write_text(body)
                subprocess.run(["cc", "-ffreestanding", "-fno-builtin", "-c",
                                str(s), "-o", str(obj)], check=True)
                return obj
            base = build("before", """
                extern int printf(void), puts(void), vsnprintf(void);
                extern int system(void), fopen(void), fclose(void);
                extern int fread(void), fwrite(void), fseek(void), ftell(void);
                extern int remove(void), rename(void);
                volatile int sink;
                void use(void) {
                  sink = printf() + puts() + vsnprintf() + system() + fopen()
                       + fclose() + fread() + fwrite() + fseek() + ftell()
                       + remove() + rename();
                }
                int in_base(void){return 1;}
            """)
            provider = build("console", """
                extern int vsnprintf(void);
                int printf(void){return vsnprintf();}
                int puts(void){return 1;}
            """)
            full = d / "after.o"
            subprocess.run(["ld", "-r", "-o", str(full), str(base), str(provider)],
                           check=True)
            output = c.inspect(base, full, provider)
            self.assertEqual(output["resolved_imports"], ["printf", "puts"])
            self.assertEqual(output["remaining_unresolved_count"], 10)
            with self.assertRaisesRegex(ValueError, "baseline already defines"):
                c.inspect(full, full, provider)
            with self.assertRaisesRegex(ValueError, "other than"):
                c.inspect(base, base, provider)
            forged = build("forged", """
                extern int vsnprintf(void);
                int printf(void){return vsnprintf();}
                int puts(void){return 1;}
                int system(void){return 0;}
            """)
            with self.assertRaisesRegex(ValueError, "unexpected symbols"):
                c.inspect(base, full, forged)
            extra = d / "extra.o"
            extra_imp = build("extra_import", """
                extern int unexpected(void);
                int call_extra(void){return unexpected();}
            """)
            subprocess.run(["ld","-r","-o",str(extra),str(full),str(extra_imp)],check=True)
            with self.assertRaisesRegex(ValueError, "other than"):
                c.inspect(base, extra, provider)


if __name__ == "__main__":
    unittest.main()
