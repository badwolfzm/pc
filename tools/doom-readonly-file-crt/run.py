#!/usr/bin/env python3
"""Build/run actual read-only FILE prototype with host stdio isolated."""
from pathlib import Path
import subprocess
import sys

here=Path(__file__).resolve().parent
out=here.parents[1]/"target"/"doom-readonly-file-crt"
out.mkdir(parents=True,exist_ok=True)
rename=["fopen","fclose","fread","fseek","ftell"]
obj=out/"file.o"
flags=["cc","-std=c11","-O1","-g","-Wall","-Wextra","-Werror","-fno-builtin",
       "-fsanitize=undefined","-fno-sanitize-recover=all"]
subprocess.run([*flags,*(f"-D{x}=warden_test_{x}" for x in rename),"-c",str(here/"file.c"),"-o",str(obj)],check=True)
exe=out/"test-file"
subprocess.run([*flags,str(here/"test_file.c"),str(obj),"-o",str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print("Host test only; no Warden native guest boot.")
