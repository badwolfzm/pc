import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('check_ctype', pathlib.Path(__file__).with_name('verify.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
BASIC = {'__ctype_toupper_loc', 'system', 'fopen', 'fread', 'fwrite', 'remove', 'rename', 'printf'}


class CtypeLinkGateTests(unittest.TestCase):
    def test_exact_one_closure(self):
        x = module.check(BASIC, BASIC - {'__ctype_toupper_loc'})
        self.assertEqual(x['resolved_symbols'], ['__ctype_toupper_loc'])
        self.assertFalse(x['warden_native_boot_verified'])

    def test_no_closure(self):
        with self.assertRaises(ValueError):
            module.check(BASIC, BASIC)

    def test_wrong_closure(self):
        with self.assertRaises(ValueError):
            module.check(BASIC, BASIC - {'__ctype_toupper_loc', 'printf'})

    def test_new_import(self):
        with self.assertRaises(ValueError):
            module.check(BASIC, (BASIC - {'__ctype_toupper_loc'}) | {'memcpy'})

    def test_hidden_host_import(self):
        with self.assertRaises(ValueError):
            module.check(BASIC, BASIC - {'__ctype_toupper_loc', 'system'})

    def test_missing_original(self):
        with self.assertRaises(ValueError):
            module.check(BASIC - {'__ctype_toupper_loc'}, BASIC - {'__ctype_toupper_loc'})


if __name__ == '__main__':
    unittest.main()
