import unittest
from measure import parse_status, parse_proc_stat, verify_qemu, descendants

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
    def test_proc_stat_parses_real_field_offsets(self):
        fields = ['S'] + ['0'] * 20
        fields[7] = '13'
        fields[9] = '5'
        fields[11] = '20'
        fields[12] = '10'
        fields[19] = '4567'
        actual = parse_proc_stat('123 (unusual ) process) ' + ' '.join(fields))
        self.assertEqual(actual, {'minor_faults':13, 'major_faults':5,
                                  'cpu_ticks':30, 'starttime_ticks':4567})
    def test_proc_stat_rejects_invalid_counters(self):
        with self.assertRaises(ValueError):
            parse_proc_stat('123 (bad) S 0')
        with self.assertRaises(ValueError):
            parse_proc_stat('123 missing closing paren')
    def test_proc_stat_refuses_negative(self):
        fields = ['S'] + ['0'] * 20
        fields[7] = '-3'
        with self.assertRaises(ValueError):
            parse_proc_stat('123 (bad) ' + ' '.join(fields))
    def test_nonexistent_pid(self):
        self.assertEqual(descendants(999999999), set())

if __name__ == '__main__': unittest.main()
