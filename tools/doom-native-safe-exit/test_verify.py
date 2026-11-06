import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("exit_verify", ROOT / "verify.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Tests(unittest.TestCase):
    def test_real_link_delta_with_adversarial_import(self):
        with tempfile.TemporaryDirectory() as temp:
            d = pathlib.Path(temp)
            def obj(name, code):
                src = d / (name + ".c")
                o = d / (name + ".o")
                src.write_text(code)
                subprocess.run(["cc", "-std=gnu11", "-ffreestanding",
                                "-fno-builtin", "-fno-pie", "-fno-pic",
                                "-c", str(src), "-o", str(o)], check=True)
                return o
            before = obj("before", """
                extern void exit(int);
                extern int system(const char*);
                extern void *fopen(const char*,const char*);
                extern int fclose(void*);
                extern unsigned long fread(void*,unsigned long,unsigned long,void*);
                extern unsigned long fwrite(const void*,unsigned long,unsigned long,void*);
                extern int fseek(void*,long,int), remove(const char*),rename(const char*,const char*);
                extern long ftell(void*);
                volatile unsigned long sink;
                void use(void) {
                    sink=(unsigned long)&exit + system(0) + (unsigned long)fopen(0,0) +
                         fclose(0) + fread(0,0,0,0) + fwrite(0,0,0,0)
                         + fseek(0,0,0) + ftell(0) + remove(0) + rename(0,0);
                }
            """)
            provider = obj("provider", "_Noreturn void exit(int x) {(void)x; for(;;) {}}")
            after = d / "after.o"
            subprocess.run(["ld", "-r", "-o", str(after), str(before), str(provider)], check=True)
            self.assertEqual(m.check(before, after, provider)["closed_exactly"], ["exit"])
            with self.assertRaisesRegex(ValueError, "baseline"):
                m.check(after, after, provider)
            unsafe = obj("unsafe", "int system(const char*s){(void)s;return 0;}")
            with self.assertRaisesRegex(ValueError, "only exit"):
                m.check(before, after, unsafe)
            surprise = obj("surprise", "extern int unexpected(void);int call(void){return unexpected();}")
            bad = d / "bad.o"
            subprocess.run(["ld", "-r", "-o", str(bad), str(after), str(surprise)], check=True)
            with self.assertRaisesRegex(ValueError, "unexpected resolved"):
                m.check(before, bad, provider)


if __name__ == "__main__":
    unittest.main()
