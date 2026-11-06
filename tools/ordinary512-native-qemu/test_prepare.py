import unittest
from prepare import SOURCE, TARGET, transform

GOOD = '#!/bin/bash\nqemu-system-x86_64 ' + SOURCE + ' -serial stdio\n'

class PrepareTests(unittest.TestCase):
    def test_exact_512_one_core(self):
        self.assertIn(TARGET, transform(GOOD))
    def test_different_source_ram_rejected(self):
        with self.assertRaises(ValueError): transform(GOOD.replace("128M", "256M"))
    def test_double_qemu_command_rejected(self):
        with self.assertRaises(ValueError): transform(GOOD + GOOD)
    def test_duplicate_m_command_rejected(self):
        with self.assertRaises(ValueError): transform(GOOD + "\n# " + SOURCE + "\n")
    def test_existing_smp_rejected(self):
        with self.assertRaises(ValueError): transform(GOOD + "\n# -smp 4\n")
    def test_no_qemu_rejected(self):
        with self.assertRaises(ValueError): transform(GOOD.replace("qemu-system-x86_64","unrelated_command"))

if __name__ == "__main__":
    unittest.main()
