import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('verified_strtod', Path(__file__).with_name('verify.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
BASE = set(module.PROTECTED) | {'strtod', '__errno_location', 'printf'}


class TestExactGenuineLinkClosure(unittest.TestCase):
    def test_exact_closure(self):
        r = module.verify(BASE, BASE - {'strtod'})
        self.assertEqual(r['resolved'], ['strtod'])
        self.assertFalse(r['ready_for_runtime_integration'])
        self.assertFalse(r['correctly_rounded_strtod'])

    def test_missing_target(self):
        with self.assertRaises(ValueError):
            module.verify(BASE - {'strtod'}, BASE - {'strtod'})

    def test_unresolved_target(self):
        with self.assertRaises(ValueError):
            module.verify(BASE, BASE)

    def test_fake_host_io_resolution(self):
        with self.assertRaises(ValueError):
            module.verify(BASE, BASE - {'strtod', 'fopen'})

    def test_fake_errno_resolution(self):
        with self.assertRaises(ValueError):
            module.verify(BASE, BASE - {'strtod', '__errno_location'})

    def test_added_dependency(self):
        with self.assertRaises(ValueError):
            module.verify(BASE, BASE - {'strtod'} | {'__tls_get_addr'})

    def test_unrelated_removed_symbol(self):
        with self.assertRaises(ValueError):
            module.verify(BASE, BASE - {'strtod', 'printf'})


if __name__ == '__main__':
    unittest.main()
