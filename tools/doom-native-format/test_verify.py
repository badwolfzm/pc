import pathlib
import subprocess
import tempfile
import unittest
import verify


class ImportCheck(unittest.TestCase):
    def test_positive_and_denials(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            def compile(name, body):
                c = root / (name + '.c')
                o = root / (name + '.o')
                c.write_text(body)
                subprocess.run(['cc', '-std=c11', '-ffreestanding', '-fno-builtin', '-c', str(c), '-o', str(o)], check=True)
                return o
            base = compile('base', '''
                extern int snprintf(void), vsnprintf(void), system(void);
                extern int fopen(void), fread(void), fwrite(void), remove(void), rename(void);
                volatile int sink;
                void use(void) { sink = snprintf()+vsnprintf()+system()+fopen()+fread()+fwrite()+remove()+rename(); }
            ''')
            provider = compile('provider', 'int snprintf(void){return 0;} int vsnprintf(void){return 0;}')
            combined = root / 'combined.o'
            subprocess.run(['ld', '-r', '-o', str(combined), str(base), str(provider)], check=True)
            r = verify.verify(base, combined, provider, root / 'report.json')
            self.assertEqual(r['missing_after'], 6)
            with self.assertRaisesRegex(ValueError, 'must contain both'):
                verify.verify(combined, combined, provider, root / 'no-target.json')
            with self.assertRaisesRegex(ValueError, 'expected only'):
                verify.verify(base, base, provider, root / 'no-change.json')
            evil = compile('evil', 'int snprintf(void){return 1;} int vsnprintf(void){return 1;} int system(void){return 1;}')
            with self.assertRaisesRegex(ValueError, 'extra'):
                verify.verify(base, combined, evil, root / 'evil.json')
            partial = compile('partial', 'int snprintf(void){return 1;}')
            with self.assertRaisesRegex(ValueError, 'extra'):
                verify.verify(base, combined, partial, root / 'partial.json')


if __name__ == '__main__':
    unittest.main()
