import importlib.util
import pathlib
import unittest

p = pathlib.Path(__file__).with_name('check.py')
spec = importlib.util.spec_from_file_location('checker', p)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

BASE = ['qemu-system-x86_64', '-smp', '1', '-m', '512M',
        '-nographic', '-kernel', '/tmp/warden-kernel.mb32']

class GateTests(unittest.TestCase):
    def test_accepted(self):
        self.assertEqual(checker.verify(BASE)['configured_guest_ram_mib'], 512)
        self.assertEqual(checker.verify(['C:/QEMU/qemu-system-x86_64.exe', '-m', '524288K',
                                         '-smp', '1,sockets=1,cores=1,threads=1'])['configured_vcpus'], 1)

    def test_reject_memory_cpu_bypass(self):
        bad = [
            BASE + ['-m', '1024M'], BASE + ['-smp', '2'],
            ['qemu-system-x86_64', '-smp', '2', '-m', '512M'],
            ['qemu-system-x86_64', '-smp', '1', '-m', '1024M'],
            ['qemu-system-x86_64', '-smp', '1,maxcpus=2', '-m', '512M'],
            BASE + ['-device', 'pc-dimm'], BASE + ['-object', 'memory-backend-ram,id=x,size=1G'],
            BASE + ['-numa', 'node,mem=512'], BASE + ['-readconfig', 'extra.cfg'],
            BASE + ['-machine', 'pc'], BASE + ['-m=1024M'],
        ]
        for argv in bad:
            with self.subTest(argv=argv), self.assertRaises(checker.ConfigError):
                checker.verify(argv)

    def test_reject_malformed(self):
        for argv in [None, [], 'qemu-system-x86_64', [1], ['qemu-system-aarch64'],
                     BASE + ['-unknown', 'x'], BASE[:-1], BASE + ['-S'],
                     BASE + ['-smp', '1,threads=0'], BASE + ['-m', '-1'],
                     BASE + ['-smp', '1,cores=2'], BASE + ['-m', '0.5G'],
                     BASE + ['-m', '512M,slots=2,maxmem=1G'],
                     BASE + ['-name', 'bad\x00name']]:
            with self.subTest(argv=argv), self.assertRaises(checker.ConfigError):
                checker.verify(argv)

if __name__ == '__main__':
    unittest.main()
