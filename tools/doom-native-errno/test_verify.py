import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("verify", ROOT / "verify.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Tests(unittest.TestCase):
    def test_real_object_delta_and_protected_call(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            def obj(name, source):
                src = root / (name + ".c")
                out = root / (name + ".o")
                src.write_text(source)
                subprocess.run(["cc", "-std=c11", "-O0", "-fno-pie",
                                "-fno-pic", "-ffreestanding", "-c",
                                str(src), "-o", str(out)], check=True)
                return out
            before = obj("before", "extern long strtol(const char*,char**,int);"
                         "extern int *__errno_location(void);"
                         "extern int system(const char*);"
                         "extern void *fopen(const char*,const char*);"
                         "extern int fclose(void*);"
                         "extern unsigned long fread(void*,unsigned long,unsigned long,void*);"
                         "extern unsigned long fwrite(const void*,unsigned long,unsigned long,void*);"
                         "extern int remove(const char*);extern int rename(const char*,const char*);"
                         "volatile long sink;void use(void){sink=strtol(\"1\",0,10)+*__errno_location()"
                         "+system(0)+(long)fopen(0,0)+fclose(0)+fread(0,0,0,0)"
                         "+fwrite(0,0,0,0)+remove(0)+rename(0,0);}")
            errno = obj("errno", "static int e; int *__errno_location(void){return &e;}")
            strtol = obj("strtol", "extern int *__errno_location(void);"
                         "long strtol(const char*s,char**e,int b){(void)s;(void)e;(void)b;return *__errno_location();}")
            after = root / "after.o"
            subprocess.run(["ld", "-r", "-o", str(after),
                            str(before), str(errno), str(strtol)], check=True)
            self.assertEqual(m.check(before, after, errno, strtol)["remaining_unresolved"],
                             len(m.DANGEROUS))
            with self.assertRaisesRegex(ValueError, "expected two-symbol"):
                m.check(after, after, errno, strtol)
            injected = obj("injected", "extern int extra(void);int call_extra(void){return extra();}")
            bad = root / "bad.o"
            subprocess.run(["ld", "-r", "-o", str(bad), str(after), str(injected)], check=True)
            with self.assertRaisesRegex(ValueError, "expected two-symbol"):
                m.check(before, bad, errno, strtol)


if __name__ == "__main__":
    unittest.main()
