import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from sample import parse_stat, parse_status, sample


class SamplerTests(unittest.TestCase):
    def test_stat_with_awkward_process_name(self):
        fields = ["S"] + ["0"] * 21
        fields[7] = "13"
        fields[9] = "2"
        fields[11] = "40"
        fields[12] = "10"
        fields[17] = "3"
        fields[21] = "27"
        result = parse_stat("123 (test ) process name) " + " ".join(fields))
        self.assertEqual((result["minor_faults"], result["major_faults"], result["cpu_ticks"], result["thread_count"], result["rss_pages"]), (13, 2, 50, 3, 27))

    def test_status(self):
        self.assertEqual(parse_status("Name: game\nVmRSS: 44 kB\nVmHWM: 50 kB\n"), {"VmRSS": 44 * 1024, "VmHWM": 50 * 1024})
        with self.assertRaises(ValueError):
            parse_status("VmRSS: 3 MB")

    @unittest.skipUnless(sys.platform.startswith("linux") and hasattr(os, "sched_setaffinity"), "Linux only")
    def test_actual_child_cpu_and_rss(self):
        cpu = min(os.sched_getaffinity(0))
        code = (
            "import os,time;"
            "assert os.sched_getaffinity(0)=={" + str(cpu) + "};"
            "data=bytearray(3*1024*1024);"
            "data[0]=1;data[-1]=2;"
            "time.sleep(0.15)"
        )
        result = sample([sys.executable, "-c", code], cpu=cpu, budget_mib=512, period_ms=10, timeout_sec=3)
        self.assertEqual(result["exit_code"], 0)
        self.assertFalse(result["timeout"])
        self.assertGreater(result["sample_count"], 0)
        self.assertGreater(result["peak_sampled_rss_bytes"], 0)
        self.assertEqual(result["cpu_affinity"], [cpu])
        self.assertIsInstance(result["minor_faults_delta"], int)

    def test_bad_parameters(self):
        with self.assertRaises(ValueError):
            sample([], 0)
        with self.assertRaises(ValueError):
            sample(["echo"], -1)
        with self.assertRaises(ValueError):
            sample(["echo"], 0, budget_mib=0)

    @unittest.skipUnless(sys.platform.startswith("linux") and hasattr(os, "sched_setaffinity"), "Linux only")
    def test_timeout_kills_workload(self):
        cpu = min(os.sched_getaffinity(0))
        r = sample([sys.executable, "-c", "import time;time.sleep(5)"], cpu=cpu, timeout_sec=0.08, period_ms=10)
        self.assertTrue(r["timeout"])
        self.assertNotEqual(r["exit_code"], 0)


if __name__ == "__main__":
    unittest.main()
