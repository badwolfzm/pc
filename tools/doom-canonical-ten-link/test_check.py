import unittest
from check import verify, verify_provider_defs, EXPECTED_PROVIDERS, REQUIRED, FORBIDDEN_HOST

class ClosureTests(unittest.TestCase):
    def setUp(self):
        self.providers={k:set(v) for k,v in EXPECTED_PROVIDERS.items()}
        self.providers["arena"]={"doom_alloc","doom_free"}
        self.before=set(REQUIRED)|set(FORBIDDEN_HOST)|{"printf","strdup"}
    def test_exact_delta(self):
        report=verify(self.before,self.before-set(REQUIRED),self.providers)
        self.assertEqual(report["remaining_unresolved"],len(self.before)-10)
        self.assertTrue(verify_provider_defs(self.providers))
    def test_duplicate_cross_provider_fails(self):
        self.providers["pure"].add("strcasecmp")
        with self.assertRaisesRegex(ValueError,"duplicate"):
            verify_provider_defs(self.providers)
    def test_missing_provider_fails(self):
        self.providers["extra"].remove("strncpy")
        with self.assertRaises(ValueError):
            verify_provider_defs(self.providers)
    def test_protected_import_disappearance_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before,self.before-set(REQUIRED)-{"system"},self.providers)
    def test_unexpected_extra_resolution_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before,self.before-set(REQUIRED)-{"printf"},self.providers)
    def test_new_undefined_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before,(self.before-set(REQUIRED))|{"surprise"},self.providers)
    def test_baseline_without_expected_symbols_rejected(self):
        with self.assertRaises(ValueError):
            verify(self.before-{"abs"},self.before-set(REQUIRED),self.providers)

if __name__=="__main__":
    unittest.main()
