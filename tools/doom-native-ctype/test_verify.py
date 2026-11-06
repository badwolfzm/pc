import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("verify", ROOT / "verify.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class ClosureTests(unittest.TestCase):
    def test_exact_one_import_closure_and_negative_cases(self):
        with tempfile.TemporaryDirectory() as td:
            td = pathlib.Path(td)
            (td / "use.c").write_text(
                "extern const int **__ctype_toupper_loc(void);\n"
                "int read_upper(void) {return (*__ctype_toupper_loc())[97];}\n")
            (td / "unexpected.c").write_text(
                "extern int extra(void); int use_extra(void) {return extra();}\n")
            cmd = ["cc", "-std=c11", "-ffreestanding", "-fno-stack-protector",
                   "-fno-pie", "-fno-pic", "-c"]
            for name in ("use", "unexpected"):
                subprocess.run([*cmd, str(td / (name + ".c")), "-o",
                                str(td / (name + ".o"))], check=True)
            subprocess.run([*cmd, str(ROOT / "ctype.c"), "-o",
                            str(td / "ctype.o")], check=True)
            subprocess.run(["ld", "-r", "-o", str(td / "good.o"),
                            str(td / "use.o"), str(td / "ctype.o")], check=True)
            self.assertEqual(mod.verify(td / "use.o", td / "good.o")
                             ["resolved_import"], mod.TARGET)
            with self.assertRaisesRegex(ValueError, "target"):
                mod.verify(td / "good.o", td / "good.o")
            subprocess.run(["ld", "-r", "-o", str(td / "bad.o"),
                            str(td / "good.o"), str(td / "unexpected.o")], check=True)
            with self.assertRaisesRegex(ValueError, "outside exactly"):
                mod.verify(td / "use.o", td / "bad.o")


if __name__ == "__main__":
    unittest.main()
