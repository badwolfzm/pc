"""Guard Linux guest entropy against regressions to fake randomness.

Source checks are not cryptographic assurance or guest-runtime tests.
Run: python3 -m unittest discover -s tests/security -p 'test_linux_failclosed_entropy.py' -v
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "crates/warden-kernel/src/linux.rs"


class FailClosedEntropy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = SOURCE.read_text(encoding="utf-8")
        cls.stack = cls.src.split("fn init_stack(", 1)[1].split("// ---- dispatch", 1)[0]
        cls.random = cls.src.split("fn getrandom(", 1)[1].split("fn clock_gettime(", 1)[0]

    def test_auxv_does_not_advertise_fabricated_at_random(self):
        self.assertNotRegex(self.stack, r"\(\s*25\s*,")
        self.assertIn("(0, 0)", self.stack)

    def test_startup_does_not_use_tick_seed(self):
        self.assertNotIn("random_at", self.stack)
        self.assertNotIn("wrapping_mul(31)", self.stack)
        self.assertNotRegex(self.stack, r"ticks\s*:\s*u64")
        self.assertNotRegex(self.src, r"init_stack\(&a,\s*app\.argv,\s*&placed,\s*k\.ticks\)")

    def test_getrandom_fails_closed_for_nonzero_requests(self):
        self.assertRegex(self.random, r"if\s+n\s*==\s*0\s*\{\s*Ok\(0\)\s*\}\s*else\s*\{\s*Err\(ENOSYS\)\s*\}")
        self.assertNotIn("copy_to_user", self.random)

    def test_no_xorshift_entropy_or_prng_state(self):
        self.assertNotIn("xorshift", self.random)
        self.assertNotIn("wrapping_mul", self.random)
        self.assertNotRegex(self.src, r"\brng\s*:\s*u64")

    def test_guest_syscall_dispatch_still_routes_to_guard(self):
        self.assertIn('"getrandom" => ret(getrandom(k, cur, a[0], a[1]))', self.src)


if __name__ == "__main__":
    unittest.main()
