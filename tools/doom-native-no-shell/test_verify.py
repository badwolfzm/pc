import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location(
    "checker", pathlib.Path(__file__).with_name("verify.py"))
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)


class Tests(unittest.TestCase):
    def test_source_pin_required(self):
        with self.assertRaises(Exception):
            v.validate_source(pathlib.Path("/does-not-exist"))

    def test_permissions_are_not_silent_success(self):
        self.assertEqual(v.PROTECTED, {"fopen","fclose","fread","fwrite",
                                        "fseek","ftell","remove","rename"})

    def test_target_is_not_shell_grant(self):
        self.assertEqual(v.TARGET, "system")


if __name__ == "__main__":
    unittest.main()
