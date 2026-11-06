import copy
import unittest
from compare import Invalid, check_two

BASE = {
    "evidence_kind": "native-warden-pong-qemu-serial",
    "guest_ram_configured_bytes": 536870912,
    "guest_vcpus_configured": 1,
    "kernel_checks_passed": 15,
    "paced_phase_frames": 90,
    "paced_phase_fps_reported": 29.99,
    "allocator_frames_in_use_at_end": 503,
    "allocator_frame_bytes_at_end": 2060288,
    "screen_hash": "0x728c34136c73c14b"
}
def pair():
    a, b = copy.deepcopy(BASE), copy.deepcopy(BASE)
    b["screen_hash"] = "0x65b35ad9ba02182b"
    return a, b

class RepeatBootTests(unittest.TestCase):
    def test_two_good_boots(self):
        a,b=pair()
        r=check_two(a,b)
        self.assertTrue(r["two_native_boots_passed"])
        self.assertEqual(len(r["reported_paced_fps"]),2)
    def test_different_screen_hashes_allowed(self):
        a,b=pair()
        self.assertTrue(check_two(a,b)["two_native_boots_passed"])
    def test_same_reference_rejected(self):
        a,b=pair()
        with self.assertRaises(Invalid):check_two(a,a)
    def test_missing_second_boot(self):
        a,b=pair()
        with self.assertRaises(Invalid):check_two(a,{})
    def test_wrong_vcpu(self):
        a,b=pair();b["guest_vcpus_configured"]=2
        with self.assertRaises(Invalid):check_two(a,b)
    def test_wrong_ram(self):
        a,b=pair();b["guest_ram_configured_bytes"]=128*1024*1024
        with self.assertRaises(Invalid):check_two(a,b)
    def test_fake_success_boolean(self):
        a,b=pair();b["kernel_checks_passed"]=True
        with self.assertRaises(Invalid):check_two(a,b)
    def test_missing_frame_hash(self):
        a,b=pair();b["screen_hash"]=""
        with self.assertRaises(Invalid):check_two(a,b)
    def test_malformed_frames(self):
        a,b=pair();b["allocator_frame_bytes_at_end"]+=1
        with self.assertRaises(Invalid):check_two(a,b)
    def test_zero_frames(self):
        a,b=pair();b["paced_phase_frames"]=0
        with self.assertRaises(Invalid):check_two(a,b)
    def test_negative_rate(self):
        a,b=pair();b["paced_phase_fps_reported"]=-1.0
        with self.assertRaises(Invalid):check_two(a,b)

if __name__=="__main__":
    unittest.main()
