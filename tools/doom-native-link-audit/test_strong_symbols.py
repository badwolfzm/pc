import tempfile
import pathlib
import subprocess
import unittest
from strong_symbols import analyze

class StrongSymbolChecks(unittest.TestCase):
    def compile(self,root,base,code):
        src=root/(base+".c")
        src.write_text(code)
        obj=root/(base+".o")
        subprocess.run(["cc","-std=c11","-ffreestanding","-fno-builtin","-fno-pie","-c",str(src),"-o",str(obj)],check=True)
        return obj
    def test_duplicate_strong_function_fails(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            a=self.compile(root,"a","int strstr(void) { return 1; }")
            b=self.compile(root,"b","int strstr(void) { return 2; }")
            report=analyze([a,b])
            self.assertIn("strstr",report["strong_duplicate_definitions"])
            self.assertEqual(report["strong_duplicate_count"],1)
    def test_distinct_symbols(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            a=self.compile(root,"a","int first(void) { return 1; }")
            b=self.compile(root,"b","int second(void) { return 2; }")
            self.assertEqual(analyze([a,b])["strong_duplicate_count"],0)
    def test_duplicate_inputs_fail(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            a=self.compile(root,"a","int first(void) { return 1; }")
            with self.assertRaises(ValueError):
                analyze([a,a])
    def test_nonexistent(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)
            with self.assertRaises(ValueError):
                analyze([p/"x.o",p/"y.o"])
if __name__=="__main__":
    unittest.main()
