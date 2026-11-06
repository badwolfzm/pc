import importlib.util
from pathlib import Path
import unittest
s=importlib.util.spec_from_file_location("audit",Path(__file__).with_name("audit.py"))
a=importlib.util.module_from_spec(s);s.loader.exec_module(a)
class Scan(unittest.TestCase):
    def test_only_real_code_not_comments_or_strings(self):
        text='/* system(fake); */\nchar *s="fopen(x)";\n// rename(x,y)\nsystem("echo hello");\n'
        h=a.candidates(text,"example.c")
        self.assertEqual([(x["function"],x["line"]) for x in h],[("system",4)])
    def test_multiline_comment_preserves_lines(self):
        text='/*\nsystem(a)\n*/\nfopen("x","r");\n'
        self.assertEqual(a.candidates(text,"x.c")[0]["line"],4)
    def test_backslash_escaped_literal(self):
        self.assertEqual(a.candidates('puts("say \\"system(\\")"); fclose(f);',"x.c")[0]["function"],"fclose")
    def test_unterminated_comment_fails(self):
        with self.assertRaises(ValueError):a.mask_noncode("/* none")
    def test_unterminated_string_fails(self):
        with self.assertRaises(ValueError):a.mask_noncode('char*s="unfinished')
    def test_real_source_manifest_is_pinned(self):
        self.assertEqual(len(a.PIN),40)
        self.assertIn("system",a.SENSITIVE)
if __name__=="__main__":unittest.main()
