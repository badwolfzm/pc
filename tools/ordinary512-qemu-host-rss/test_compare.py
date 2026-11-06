import unittest
from compare import compare

def report(rss=84660224):
    return {
        'evidence_kind':'actual-native-warden-qemu-host-process',
        'guest_ram_option_mib':512,'guest_vcpus_option':1,
        'qemu_observed':True,'timed_out':False,'child_exit_code':0,
        'samples':366,'qemu_host_peak_sampled_rss_bytes':rss,
        'qemu_host_peak_reported_hwm_bytes':rss,
        'qemu_host_peak_thread_count':3,
        'qemu_cmdline_sha256':'a'*64,
        'qemu_host_minor_fault_delta_observed':8098,
        'qemu_host_major_fault_delta_observed':39,
        'qemu_host_cpu_ticks_delta_observed':740,
        'qemu_observed_span_seconds':7.535,
        'launcher_elapsed_seconds_including_build':11.809
    }

class Tests(unittest.TestCase):
    def test_two_valid_distinct_rss(self):
        r=compare(report(),report(90000000))
        self.assertTrue(r['both_qemu_runs_exited_successfully'])
        self.assertEqual(r['host_minor_fault_deltas_per_run'], [8098,8098])
        self.assertEqual(r['host_major_fault_deltas_per_run'], [39,39])
        self.assertEqual(r['host_cpu_tick_deltas_per_run'], [740,740])
        self.assertEqual(r['qemu_observed_spans_seconds'], [7.535,7.535])
        self.assertEqual(r['max_host_peak_sampled_rss_bytes'],90000000)
        self.assertEqual(r['host_peak_rss_spread_bytes'],90000000-84660224)
    def test_identical_rss_is_valid(self):
        self.assertEqual(compare(report(),report())['host_peak_rss_spread_bytes'],0)
    def test_wrong_cpu(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'guest_vcpus_option':2})
    def test_wrong_ram(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'guest_ram_option_mib':128})
    def test_missing_qemu(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'qemu_observed':False})
    def test_failed_exit(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'child_exit_code':1})
    def test_inconsistent_hwm(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'qemu_host_peak_reported_hwm_bytes':1})
    def test_missing_hash(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'qemu_cmdline_sha256':''})
    def test_negative_host_counter_rejected(self):
        with self.assertRaises(ValueError):
            compare(report(), {**report(), 'qemu_host_major_fault_delta_observed':-1})
    def test_missing_host_counter_rejected(self):
        r=report()
        del r['qemu_host_minor_fault_delta_observed']
        with self.assertRaises(ValueError):
            compare(report(),r)
    def test_invalid_observed_duration_rejected(self):
        with self.assertRaises(ValueError):
            compare(report(), {**report(), 'qemu_observed_span_seconds':float('nan')})
    def test_observed_longer_than_launcher_rejected(self):
        with self.assertRaises(ValueError):
            compare(report(), {**report(), 'qemu_observed_span_seconds':100})
    def test_bool_not_numeric(self):
        with self.assertRaises(ValueError): compare(report(),{**report(),'samples':True})

if __name__=='__main__': unittest.main()
