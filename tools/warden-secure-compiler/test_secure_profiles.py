import tempfile
import unittest
from pathlib import Path
from secure_profiles import Refused, plan, main

class SecurityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root/'hello.c').write_text('int add(int a,int b){return a+b;}')
    def tearDown(self): self.tmp.cleanup()
    def test_profiles(self):
        for p in ('matrixos', 'runtime-fast', 'size'):
            obj = plan(self.root, 'hello.c', p, 'baseline')
            self.assertFalse(obj['executes_compiler'])
            self.assertEqual(len(obj['source']['sha256']), 64)
            self.assertNotIn('faststripcc', obj['compiler_driver'])
    def test_faststrip_refused(self):
        with self.assertRaises(Refused): plan(self.root, 'hello.c','faststripcc','baseline')
    def test_absolute_refused(self):
        with self.assertRaises(Refused): plan(self.root,str(self.root/'hello.c'),'matrixos','baseline')
    def test_traversal(self):
        with self.assertRaises(Refused): plan(self.root,'../hello.c','matrixos','baseline')
    def test_option_like(self):
        (self.root/'-bad.c').write_text('int x;')
        with self.assertRaises(Refused): plan(self.root,'-bad.c','matrixos','baseline')
    def test_symlink(self):
        (self.root/'link.c').symlink_to(self.root/'hello.c')
        with self.assertRaises(Refused): plan(self.root,'link.c','matrixos','baseline')
    def test_subdir_symlink(self):
        (self.root/'sym').symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(Refused): plan(self.root,'sym/hello.c','matrixos','baseline')
    def test_oversize(self):
        (self.root/'huge.c').write_bytes(b'x'*(2*1024*1024+1))
        with self.assertRaises(Refused): plan(self.root,'huge.c','matrixos','baseline')
    def test_empty(self):
        (self.root/'empty.c').touch()
        with self.assertRaises(Refused): plan(self.root,'empty.c','matrixos','baseline')
    def test_wrong_extension(self):
        (self.root/'evil.so').write_text('a')
        with self.assertRaises(Refused): plan(self.root,'evil.so','matrixos','baseline')
    def test_bad_tier(self):
        with self.assertRaises(Refused): plan(self.root,'hello.c','matrixos','native')
    def test_explicit_plan_only(self):
        self.assertEqual(main(['--trusted-source-root',str(self.root),'--source','../nope.c']),2)

if __name__=='__main__': unittest.main()
