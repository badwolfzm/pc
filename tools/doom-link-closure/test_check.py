import unittest
from check import inventory_symbols, parse_nm, compare

class Tests(unittest.TestCase):
    def test_inventory(self):
        self.assertEqual(inventory_symbols({'unresolved_external_symbols':['memcpy','system']}), {'memcpy','system'})
    def test_duplicate_rejected(self):
        with self.assertRaises(ValueError): inventory_symbols({'unresolved_external_symbols':['memcpy','memcpy']})
    def test_invalid_symbol_rejected(self):
        with self.assertRaises(ValueError): inventory_symbols({'unresolved_external_symbols':['bad;rm']})
    def test_missing_schema(self):
        with self.assertRaises(ValueError): inventory_symbols({'files':[]})
    def test_nm_parses_strong_weak(self):
        self.assertEqual(parse_nm('memcpy T 0000 0010\nmemmove W 0010 0002\nputs U\n'), {'memcpy','memmove'})
    def test_nm_skips_bad_input(self):
        self.assertEqual(parse_nm('test.o:\nmemcpy U\nlocal t 0100\n@@bad T 0010\n'), set())
    def test_partial(self):
        r=compare({'memcpy','system'}, {'memcpy'})
        self.assertEqual(r['matched_symbols'], ['memcpy'])
        self.assertEqual(r['still_missing'], ['system'])
        self.assertFalse(r['complete'])
        self.assertEqual(r['unsafe_host_services_require_explicit_policy'], ['system'])
    def test_complete(self):
        self.assertTrue(compare({'a'}, {'a'})['complete'])

if __name__ == '__main__': unittest.main()
