import unittest
from gate import defined_strong,providers,verify,EXPECTED,PROTECTED,PURE_ONLY,STRING_ONLY,HEAP_ONLY
class Contracts(unittest.TestCase):
    def setUp(self):
        self.original=set(EXPECTED)|set(PROTECTED)|{"unknown"}
        self.new=self.original-set(EXPECTED)
        self.own={**{s:"pure" for s in PURE_ONLY},
                  **{s:"strings" for s in STRING_ONLY},
                  **{s:"heap-alias" for s in HEAP_ONLY}}
    def test_nm_parser(self):
        self.assertEqual(defined_strong("foo T 0000 0010\nbar B 0100 0008\nbaz W 00 00"),{"foo","bar"})
        with self.assertRaises(ValueError):defined_strong("bad U")
    def test_duplicate_object_detected(self):
        with self.assertRaisesRegex(ValueError,"duplicate strong provider"):
            providers([("module-one",{"strncpy"}),("module-two",{"strncpy"})])
    def test_weak_ignored(self):
        self.assertEqual(defined_strong("foo W 0 1"),set())
    def test_valid_composition(self):
        d=verify(self.original,self.new,self.own)
        self.assertEqual(d["baseline_undefined_count"]-d["combined_undefined_count"],10)
        self.assertFalse(d["native_executable"])
    def test_cannot_keep_unresolved_target(self):
        with self.assertRaises(ValueError):verify(self.original,self.new|{"malloc"},self.own)
    def test_cannot_mask_host_process(self):
        with self.assertRaises(ValueError):verify(self.original,self.new-{"system"},self.own)
    def test_cannot_create_new_external(self):
        with self.assertRaises(ValueError):verify(self.original,self.new|{"evil"},self.own)
    def test_wrong_provider_refused(self):
        with self.assertRaisesRegex(ValueError,"unexpected provider"):
            verify(self.original,self.new,{**self.own,"strstr":"pure"})
    def test_missing_provider_refused(self):
        d=dict(self.own);del d["abs"]
        with self.assertRaisesRegex(ValueError,"lacks provider"):verify(self.original,self.new,d)
    def test_missing_protected_original_refused(self):
        with self.assertRaisesRegex(ValueError,"protected"):
            verify(self.original-{"system"},self.new,self.own)
if __name__=="__main__":unittest.main()
