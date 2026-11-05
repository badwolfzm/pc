#!/usr/bin/env python3
"""Host-only reproducibility gate. Does not assert game or Warden compatibility."""
import pathlib
import re
import subprocess
import sys
import unittest

BIN = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else pathlib.Path("/tmp/warden-game-frame-probe")
sys.argv = sys.argv[:1]

def run(*args):
    return subprocess.run([str(BIN), *args], capture_output=True, text=True, check=False)

class Probe(unittest.TestCase):
    def test_repeated_runs_are_deterministic(self):
        for frames in ("1", "60", "300"):
            first, second = run(frames), run(frames)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            for key in ("digest", "bytes", "frames", "width", "height"):
                pat = rf"\b{key}=([^ ]+)"
                self.assertEqual(re.search(pat, first.stdout).group(1), re.search(pat, second.stdout).group(1))
            self.assertIn("single_thread=1", first.stdout)
            self.assertIn("host_only=1", first.stdout)

    def test_invalid_input_refused(self):
        for args in [(), ("0",), ("1001",), ("-1",), ("x",), ("3x",), ("1", "2")]:
            self.assertNotEqual(run(*args).returncode, 0, args)

    def test_frame_count_affects_digest(self):
        digests = [re.search(r"digest=([0-9a-f]+)", run(n).stdout).group(1) for n in ("1", "2", "60")]
        self.assertEqual(len(set(digests)), 3)

    def test_300_frame_golden_digest(self):
        """Pinned from Linux CI baseline; changes require explicit review."""
        result = run("300")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("digest=9d9771fe2682abc9", result.stdout)

    def test_framebuffer_size(self):
        self.assertIn("bytes=256000", run("2").stdout)

if __name__ == "__main__":
    unittest.main()
