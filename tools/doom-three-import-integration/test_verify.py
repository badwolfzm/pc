import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("validator", ROOT / "verify.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class IntegrationContract(unittest.TestCase):
    def test_real_object_link_and_adversarial_cases(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            def build(name, source):
                file = base / (name + ".c")
                obj = base / (name + ".o")
                file.write_text(source)
                subprocess.run(["cc", "-std=c11", "-O0", "-ffreestanding",
                                "-fno-pie", "-fno-pic", "-c", str(file),
                                "-o", str(obj)], check=True)
                return obj
            original = build("original", """
                extern const int **__ctype_toupper_loc(void);
                extern int *__errno_location(void);
                extern long strtol(const char*, char**, int);
                extern int system(const char*);
                extern void *fopen(const char*, const char*);
                extern int fclose(void*);
                extern unsigned long fread(void*,unsigned long,unsigned long,void*);
                extern unsigned long fwrite(const void*,unsigned long,unsigned long,void*);
                extern int fseek(void*,long,int);
                extern long ftell(void*);
                extern int remove(const char*),rename(const char*,const char*);
                volatile long sink;
                void use(void) {
                    sink=(*__ctype_toupper_loc())[97]+*__errno_location()
                      +strtol("1",0,10)+system(0)+(long)fopen(0,0)+fclose(0)
                      +fread(0,0,0,0)+fwrite(0,0,0,0)+fseek(0,0,0)
                      +ftell(0)+remove(0)+rename(0,0);
                }
            """)
            ctype = build("ctype", "static const int *p;const int **__ctype_toupper_loc(void){return &p;}")
            errno = build("errno", "static int e; int *__errno_location(void){return &e;}")
            strtol = build("strtol", "extern int *__errno_location(void);"
                "long strtol(const char *s,char **end,int base){(void)s;(void)end;(void)base;"
                "return *__errno_location();}")
            providers = {"ctype": ctype, "errno": errno, "strtol": strtol}
            after = base / "linked.o"
            subprocess.run(["ld", "-r", "-o", str(after),
                            str(original), str(ctype), str(errno), str(strtol)], check=True)
            report = m.verify(original, after, providers)
            self.assertEqual(report["original_unresolved"], 12)
            self.assertEqual(report["remaining_unresolved"], 9)
            self.assertFalse(report["native_warden_doom_boot"])
            with self.assertRaisesRegex(ValueError, "baseline"):
                m.verify(after, after, providers)
            extra = build("extra", "extern int untrusted(void);int use_extra(void){return untrusted();}")
            bad = base / "bad.o"
            subprocess.run(["ld", "-r", "-o", str(bad), str(after), str(extra)], check=True)
            with self.assertRaisesRegex(ValueError, "unexpected change"):
                m.verify(original, bad, providers)
            bad_provider = build("bad_provider",
                "extern int host(void);int __errno_location(void){return host();}")
            with self.assertRaisesRegex(ValueError, "provider owns"):
                m.verify(original, after, {**providers, "errno": bad_provider})
            masked = build("masked", "int system(const char*p){(void)p;return 0;}")
            masked_link = base / "masked.o"
            subprocess.run(["ld", "-r", "-o", str(masked_link), str(after), str(masked)], check=True)
            with self.assertRaisesRegex(ValueError, "unexpected change"):
                m.verify(original, masked_link, providers)


if __name__ == "__main__":
    unittest.main()
