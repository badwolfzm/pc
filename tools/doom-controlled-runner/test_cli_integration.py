"""Full CLI integration tests with a deliberately FAKE QEMU script.

This exercises the real runner's subprocess and evidence path without booting
QEMU, Warden, Doom, or a real kernel. Never use these fixture results as game proof.
"""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

RUNNER = pathlib.Path(__file__).resolve().with_name("run.py")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class FullCliFixtureTests(unittest.TestCase):
    def invoke(self, root, code=1, corrupt_kernel_hash=False):
        root = pathlib.Path(root)
        fake = root / "qemu-system-x86_64"
        fake.write_text(
            "#!" + sys.executable + "\n"
            "import sys\n"
            "args=sys.argv[1:]\n"
            "assert args[args.index('-smp')+1]=='1'\n"
            "assert args[args.index('-m')+1]=='512M'\n"
            "assert args[args.index('-accel')+1]=='tcg'\n"
            "print('FIXTURE ONLY: no genuine Warden boot',flush=True)\n"
            "sys.exit(" + str(code) + ")\n",
            encoding="utf-8",
        )
        fake.chmod(0o700)
        kernel = root / "mock-kernel.mb32"
        kernel.write_bytes(b"NOT A REAL WARDEN KERNEL")
        log, report = root / "serial.log", root / "report.json"
        cmd = [sys.executable, str(RUNNER),
               "--qemu", str(fake), "--qemu-sha256", sha(fake),
               "--kernel", str(kernel),
               "--kernel-sha256", "0"*64 if corrupt_kernel_hash else sha(kernel),
               "--accel", "tcg", "--timeout", "5",
               "--log", str(log), "--report", str(report)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        return result, log, report, cmd

    def test_fake_debug_exit_one_writes_provenance_but_no_boot_claim(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, log, report, _ = self.invoke(tmp, 1)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("FIXTURE ONLY", log.read_text())
            data = json.loads(report.read_text())
            self.assertEqual(data["returncode"], 1)
            self.assertEqual(data["debug_exit_status"], 0)
            self.assertTrue(data["warden_debug_exit_zero"])
            self.assertFalse(data["guest_boot_verified"])
            self.assertFalse(data["doom_executed_verified"])
            self.assertEqual(data["serial_log_sha256"], sha(log))
            self.assertEqual(data["serial_log_bytes"], len(log.read_bytes()))

    def test_fake_debug_exit_three_is_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, _, report, _ = self.invoke(tmp, 3)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(json.loads(report.read_text())["debug_exit_status"], 1)

    def test_fake_host_zero_is_not_debug_exit_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, _, report, _ = self.invoke(tmp, 0)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIsNone(json.loads(report.read_text())["debug_exit_status"])

    def test_bad_kernel_hash_refuses_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            result, log, report, _ = self.invoke(tmp, 1, corrupt_kernel_hash=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("SHA-256 mismatch", result.stderr)
            self.assertFalse(log.exists())
            self.assertFalse(report.exists())

    def test_evidence_cannot_be_overwritten_by_second_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, log, report, cmd = self.invoke(tmp, 1)
            self.assertEqual(first.returncode, 0, first.stderr)
            original_log = log.read_bytes()
            original_report = report.read_bytes()
            second = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
            self.assertEqual(second.returncode, 2)
            self.assertEqual(log.read_bytes(), original_log)
            self.assertEqual(report.read_bytes(), original_report)


if __name__ == "__main__":
    unittest.main()
