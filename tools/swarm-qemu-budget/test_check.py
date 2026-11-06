import unittest
from check import validate, memory_mib

class LaunchBudgetTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(validate(['qemu-system-x86_64', '-smp', '1', '-m', '512M']),
                         {'vcpus': 1, 'guest_ram_mib': 512})
        self.assertEqual(validate(['qemu-system-x86_64.exe', '-machine', 'q35,accel=whpx',
                                   '-smp', '1', '-m', '256M', '-kernel', 'warden.mb32',
                                   '-nographic']), {'vcpus': 1, 'guest_ram_mib': 256})

    def test_memory(self):
        self.assertEqual(memory_mib('524288k'), 512)
        for bad in ('0', '1k', '513M', '1G', '512.1M', '-1M', 'garbage'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                memory_mib(bad)

    def test_invalid(self):
        prefix = ['qemu-system-x86_64', '-smp', '1', '-m', '512M']
        cases = [
            ['qemu-system-x86_64', '-smp', '2', '-m', '512M'],
            ['qemu-system-x86_64', '-smp', '1'],
            prefix + ['-m', '128M'],
            prefix + ['-readconfig', 'other.cfg'],
            prefix + ['-object', 'memory-backend-ram,id=x,size=2G'],
            prefix + ['-machine', 'q35,memory-backend=mem0'],
            prefix + ['-numa', 'node,memdev=x'],
            prefix + ['-device', 'virtio-vga'],
            prefix + ['-smp', '2'],
            prefix + ['-display', 'vnc=:0'],
            prefix + ['-accel', 'something'],
            prefix + ['-nographic', '-nographic'],
            ['qemu-system-aarch64', '-smp', '1', '-m', '512M'],
        ]
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                validate(case)

if __name__ == '__main__':
    unittest.main()
