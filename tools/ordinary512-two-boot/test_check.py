import unittest
from check import combine
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'ordinary512-native-qemu'))
from evidence import Invalid

MANIFEST = {
    'profile': 'native-pong-qemu-512m-smp1',
    'qemu_option': '-m 512M -smp 1',
    'guest_ram_bytes': 512*1024*1024,
    'guest_vcpus': 1
}
def log(hash_value='0x728c34136c73c14b', frames=503):
    return (
        'pong: phase A, gameplay paced to 30 fps for 3 s: 90 frames = 29.99 fps, 6 tiles per frame\n'
        f'pong: screen hash {hash_value}, kernel heap carved 0 more bytes, {frames} frames ({frames*4} KiB) of physical memory in use, 724 timer ticks\n'
        'ALL HELD: 15 checks, 0 failed\n'
    )

class Tests(unittest.TestCase):
    def test_two_boots(self):
        result = combine(log(), log(frames=504), MANIFEST)
        self.assertEqual(result['matched_screen_hash'], '0x728c34136c73c14b')
        self.assertEqual(len(result['runs']), 2)
    def test_identical_serial_is_allowed(self):
        # A deterministic guest can produce byte-identical output on genuine separate boots.
        self.assertEqual(combine(log(), log(), MANIFEST)['runs'][0]['kernel_checks_passed'], 15)
    def test_screen_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError, 'hashes disagree'):
            combine(log(), log(hash_value='0x1111111111111111'), MANIFEST)
    def test_missing_run_fails(self):
        with self.assertRaises(ValueError):
            combine(log(), '', MANIFEST)
    def test_wrong_cpu(self):
        with self.assertRaises(Invalid):
            combine(log(), log(), {**MANIFEST, 'guest_vcpus': 2})
    def test_bad_kernel_summary(self):
        with self.assertRaises(Invalid):
            combine(log(), log().replace('ALL HELD: 15 checks, 0 failed', 'ALL HELD: 14 checks, 1 failed'), MANIFEST)
    def test_large_serial_rejected(self):
        with self.assertRaisesRegex(ValueError, 'oversized'):
            combine(log(), 'a'*(2*1024*1024+1), MANIFEST)

if __name__ == '__main__':
    unittest.main()
