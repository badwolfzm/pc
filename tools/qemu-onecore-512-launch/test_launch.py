import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import sys
import os
import subprocess

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("runner", HERE / "launch.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.binary = self.root / "qemu-system-x86_64"
        self.evidence = self.root / "invoked.json"
        self.binary.write_text("#!" + sys.executable + "\n"
                               "import sys, json\n"
                               "from pathlib import Path\n"
                               "Path(" + repr(str(self.evidence)) + ").write_text(json.dumps(sys.argv[1:]))\n"
                               "sys.exit(7)\n")
        self.binary.chmod(0o700)
        self.argv = [str(self.binary), "-smp", "1", "-m", "512M", "-nographic"]

    def test_real_subprocess_invocation_preserves_validated_args_and_exit(self):
        result = runner.run(self.argv, 5)
        self.assertEqual(result["process_exit_code"], 7)
        self.assertFalse(result["doom_gameplay_verified"])
        self.assertEqual(json.loads(self.evidence.read_text()), self.argv[1:])

    def test_invalid_configuration_never_executes(self):
        for bad in [self.argv + ["-m", "1G"], self.argv + ["-device", "pc-dimm"],
                    [self.argv[0], "-smp", "2", "-m", "512M"]]:
            with self.subTest(argv=bad), self.assertRaises(ValueError):
                runner.run(bad, 5)
            self.assertFalse(self.evidence.exists())
        with self.assertRaises(ValueError):
            runner.run(["qemu-system-x86_64", "-smp", "1", "-m", "512M"], 5)
        self.assertFalse(self.evidence.exists())

    def test_bad_timeout_and_untrusted_executable(self):
        for timeout in [0, 3601, True, 1.5]:
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                runner.run(self.argv, timeout)
        self.assertFalse(self.evidence.exists())
        self.binary.unlink()
        with self.assertRaises(OSError):
            runner.run(self.argv, 5)
        self.assertFalse(self.evidence.exists())

    def test_timeout_terminates_mock(self):
        self.binary.write_text("#!" + sys.executable + "\nimport time\ntime.sleep(10)\n")
        self.binary.chmod(0o700)
        with self.assertRaises(subprocess.TimeoutExpired):
            runner.run(self.argv, 1)

if __name__ == "__main__":
    unittest.main()
