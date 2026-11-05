"""Source regression guard for Linux guest startup entropy.

This is a source-structure check, NOT proof that guests have safe entropy.
Run: python3 -m unittest discover -s tests/security -p 'test_linux_auxv_random.py'
"""
from pathlib import Path
import re
import unittest

SOURCE = Path(__file__).resolve().parents[2] / "crates/warden-kernel/src/linux.rs"


class AuxvEntropyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = SOURCE.read_text(encoding="utf-8")
        cls.stack = cls.text.split("fn init_stack(", 1)[1].split("// ---- dispatch", 1)[0]

    def test_no_at_random_key_in_linux_initial_auxv(self):
        # The source comment may mention AT_RANDOM; only prohibit the key.
        self.assertNotRegex(self.stack, r"\(\s*25\s*,")
        self.assertIn("(0, 0)", self.stack, "AT_NULL terminator required")

    def test_no_tick_seed_parameter_or_generator(self):
        self.assertNotRegex(self.stack, r"ticks\s*:\s*u64")
        self.assertNotRegex(self.stack, r"wrapping_mul\s*\(\s*31\s*\)")
        self.assertNotIn("random_at", self.stack)

    def test_loader_does_not_pass_clock_as_entropy(self):
        self.assertRegex(self.text, r"init_stack\(&a,\s*app\.name\.as_bytes\(\),\s*&placed\)")
        self.assertNotRegex(self.text, r"init_stack\([^;]*k\.ticks")

    def test_stack_retains_auxv_and_argc(self):
        self.assertIn("(3, placed.phdr)", self.stack)
        self.assertIn("(9, placed.entry)", self.stack)
        self.assertRegex(self.stack, r"put\(1\)\s*&&\s*put\(argv0_at\)")


if __name__ == "__main__":
    unittest.main()
