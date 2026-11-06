import unittest
from measure import parse_status, verify_qemu, descendants

class Tests(unittest.TestCase):
    def test_status(self):
        self.assertEqual(parse_status('VmRSS: 128 kB\nVmHWM: 256 kB\nThreads: 3\n'),
                         {'VmRSS':131072,'VmHWM':262144,'Threads':3})
    def test_invalid_unit(self):
        with self.assertRaises(ValueError): parse_status('VmRSS: 2 MB\n')
    def test_correct_flags(self):
        self.assertTrue(verify_qemu(['qemu-system-x86_64','-m','512M','-smp','1']))
    def test_wrong_ram(self):
        with self.assertRaises(ValueError): verify_qemu(['qemu-system-x86_64','-m','128M','-smp','1'])
    def test_two_cpus(self):
        with self.assertRaises(ValueError): verify_qemu(['qemu-system-x86_64','-m','512M','-smp','2'])
    def test_duplicate_ram(self):
        with self.assertRaises(ValueError): verify_qemu(['qemu-system-x86_64','-m','512M','-m','512M','-smp','1'])
    def test_nonexistent_pid(self):
        self.assertEqual(descendants(999999999), set())

if __name__ == '__main__': unittest.main()
