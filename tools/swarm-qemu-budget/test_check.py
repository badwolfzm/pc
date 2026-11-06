import unittest
from check import validate, memory_mib

class LaunchBudgetTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(validate(["qemu-system-x86_64", "-smp", "1", "-m", "512M"]), {"vcpus": 1, "guest_ram_mib": 512})
    def test_small(self):
        self.assertEqual(memory_mib("256M"), 256)
    def test_invalid(self):
        cases = [
            ["qemu-system-x86_64", "-smp", "2", "-m", "512M"],
            ["qemu-system-x86_64", "-smp", "1", "-m", "513M"],
            ["qemu-system-x86_64", "-smp", "1", "-m", "1G"],
            ["qemu-system-x86_64", "-smp", "1"],
            ["qemu-system-x86_64", "-smp", "1", "-m", "512M", "-m", "128M"],
            ["qemu-system-x86_64", "-smp", "1", "-m", "512M", "-readconfig", "other.cfg"],
        ]
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                validate(case)

if __name__ == "__main__":
    unittest.main()
