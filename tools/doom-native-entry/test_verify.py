import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", ROOT/"verify.py")
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)

class Tests(unittest.TestCase):
    def test_real_object_positive_and_negative(self):
        with tempfile.TemporaryDirectory() as temp:
            d=pathlib.Path(temp)
            def build(name, src):
                c=d/(name+".c")
                o=d/(name+".o")
                c.write_text(src)
                subprocess.run(["cc","-std=c11","-ffreestanding","-fno-builtin",
                                "-fno-pic","-fno-pie","-c",str(c),"-o",str(o)],check=True)
                return o
            base=build("base", """extern void exit(int);
                void doomgeneric_Create(int a,char**b){(void)a;(void)b;}
                void doomgeneric_Tick(void){}
                volatile void *unused=(void*)&exit;""")
            entry=build("entry", """extern void doomgeneric_Create(int,char**);
                extern void doomgeneric_Tick(void);
                extern _Noreturn void exit(int);
                _Noreturn void _start(void){
                    doomgeneric_Create(0,0);doomgeneric_Tick();exit(0);
                    for(;;) {}
                }""")
            ex=build("exit", "_Noreturn void exit(int x){(void)x;for(;;){}}")
            with self.assertRaisesRegex(ValueError, "host operations"):
                linked=d/"linked.o"
                subprocess.run(["ld","-r","-o",str(linked),
                                str(base),str(entry),str(ex)],check=True)
                v.verify(base,linked,entry,ex)
            with self.assertRaisesRegex(ValueError, "unexpected"):
                v.verify(base,base,entry,ex)
            bad=build("bad", "void rogue(void){}")
            with self.assertRaisesRegex(ValueError, "native entry"):
                v.verify(base,linked,bad,ex)

if __name__=="__main__":unittest.main()
