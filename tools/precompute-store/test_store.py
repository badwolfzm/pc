import importlib.util
import os
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("store", pathlib.Path(__file__).with_name("store.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Store, StoreError = module.Store, module.StoreError

class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
    def tearDown(self):
        self.tmp.cleanup()
    def test_roundtrip_and_dedup(self):
        with Store(self.root,"game") as s:
            a = s.put(b"static-texture")
            self.assertEqual(a, s.put(b"static-texture"))
            self.assertEqual(s.get(a), b"static-texture")
            self.assertEqual(len(list((self.root/"game"/"objects").iterdir())), 1)
    def test_namespaces_isolated(self):
        with Store(self.root,"alpha") as a, Store(self.root,"beta") as b:
            h = a.put(b"a")
            with self.assertRaises(FileNotFoundError):
                b.get(h)
    def test_quota_and_oversize(self):
        with Store(self.root,"small",max_object=5,quota=7) as s:
            s.put(b"hello")
            with self.assertRaises(StoreError):
                s.put(b"world")
            with self.assertRaises(StoreError):
                s.put(b"123456")
    def test_reject_untrusted_names(self):
        for name in ("../escape", "/etc", ".", "", "a/b", "bad name"):
            with self.subTest(name=name), self.assertRaises(StoreError):
                Store(self.root,name)
    def test_corruption_rejected(self):
        with Store(self.root,"game") as s:
            h=s.put(b"original")
            (self.root/"game"/"objects"/h).write_bytes(b"modified")
            with self.assertRaises(StoreError):
                s.get(h)
            with self.assertRaises(StoreError):
                s.put(b"original")
    def test_symlink_object_denied(self):
        with Store(self.root,"game") as s:
            h="a"*64
            os.symlink("/etc/passwd",self.root/"game"/"objects"/h)
            with self.assertRaises(OSError):
                s.get(h)
    def test_symlink_namespace_denied(self):
        os.symlink(self.root, self.root/"evil")
        with self.assertRaises(OSError):
            Store(self.root,"evil")
    def test_readonly_fails(self):
        with Store(self.root,"game") as s:
            with self.assertRaises(StoreError):
                s.get("../etc/passwd")
    def test_manifest(self):
        with Store(self.root,"game") as s:
            p=s.put(b"compiled reusable texture")
            manifest=s.put_manifest("texture",{"source":p},p,"v1")
            self.assertIn(b'"schema":1',s.get(manifest))
            with self.assertRaises(StoreError):
                s.put_manifest("texture",{"../bad":p},p,"v1")
    def test_content_addressing(self):
        with Store(self.root,"game") as s:
            self.assertEqual(s.put(b""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
            with self.assertRaises(StoreError):
                s.get("A"*64)
    def test_reject_hard_link(self):
        with Store(self.root,"game") as s:
            h=s.put(b"shared")
            os.link(self.root/"game"/"objects"/h,self.root/"outside")
            with self.assertRaises(StoreError):
                s.get(h)

if __name__=="__main__":
    unittest.main()
