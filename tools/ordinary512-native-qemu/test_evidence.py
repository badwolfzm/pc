import unittest
from evidence import Invalid, extract

MANIFEST = {"profile": "native-pong-qemu-512m-smp1",
            "qemu_option": "-m 512M -smp 1",
            "guest_ram_bytes": 536870912, "guest_vcpus": 1}
LOG = (
    "pong: phase A, gameplay paced to 30 fps for 3 s: 90 frames = 29.99 fps, 6 tiles per frame\n"
    "pong: screen hash 0x728c34136c73c14b, kernel heap carved 0 more bytes, 503 frames (2012 KiB) of physical memory in use, 724 timer ticks\n"
    "ALL HELD: 15 checks, 0 failed\n"
)
class EvidenceTests(unittest.TestCase):
    def test_actual_log_shape(self):
        r=extract(LOG, MANIFEST)
        self.assertEqual(r["allocator_frame_bytes_at_end"], 503*4096)
        self.assertEqual(r["paced_phase_frames"], 90)
    def test_memory_not_total(self):
        self.assertIn("NOT full physical RAM",extract(LOG,MANIFEST)["scope"])
    def test_mismatched_kib_rejected(self):
        with self.assertRaises(Invalid):extract(LOG.replace("2012 KiB","2011 KiB"),MANIFEST)
    def test_missing_summary_rejected(self):
        with self.assertRaises(Invalid):extract(LOG.replace("ALL HELD","NOT ALL HELD"),MANIFEST)
    def test_duplicate_summary_rejected(self):
        with self.assertRaises(Invalid):extract(LOG+"ALL HELD: 15 checks, 0 failed\n",MANIFEST)
    def test_other_core_rejected(self):
        with self.assertRaises(Invalid):extract(LOG,{**MANIFEST,"guest_vcpus":2})
    def test_other_ram_rejected(self):
        with self.assertRaises(Invalid):extract(LOG,{**MANIFEST,"guest_ram_bytes":134217728})
    def test_fail_line_rejected(self):
        with self.assertRaises(Invalid):extract(LOG+"FAIL hostile forged grant\n",MANIFEST)
    def test_zero_paced_frames_rejected(self):
        with self.assertRaises(Invalid):extract(LOG.replace("90 frames =","0 frames ="),MANIFEST)

if __name__=="__main__":
    unittest.main()
