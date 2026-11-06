import copy
import unittest
from check import LIMIT, Invalid, validate, CATEGORIES

def valid():
    return {
        "schema": "ordinary512-v1", "platform": "warden-qemu",
        "configuration": {"vcpus": 1, "guest_ram_bytes": LIMIT,
                          "graphics_backend": "cpu-software",
                          "authorized_swap_bytes": 10**12},
        "provenance": {"commit": "a" * 40, "game_sha256": "b" * 64,
                       "source": "synthetic test fixture only",
                       "telemetry_kind": "synthetic-fixture"},
        "samples": [
            {"t_ms": 0, "resident": {name: 0 for name in CATEGORIES},
             "swap_read_bytes": 0, "swap_written_bytes": 0, "page_faults": 0},
            {"t_ms": 16, "resident": {**{name: 0 for name in CATEGORIES},
                                    "game": LIMIT}, "swap_read_bytes": 0,
             "swap_written_bytes": 0, "page_faults": 0}
        ]
    }

class EnvelopeTests(unittest.TestCase):
    def test_exact_512_mib_allowed(self):
        r = validate(valid())
        self.assertEqual(r["headroom_at_peak_bytes"], 0)
        self.assertEqual(r["samples_checked"], 2)
    def test_single_byte_over_limit_rejected(self):
        d = valid()
        d["samples"][1]["resident"]["kernel"] = 1
        with self.assertRaisesRegex(Invalid, "exceeds"): validate(d)
    def test_double_count_is_not_hidden(self):
        d = valid()
        d["samples"][1]["resident"]["game"] = LIMIT - 100
        d["samples"][1]["resident"]["page_cache"] = 101
        with self.assertRaisesRegex(Invalid, "exceeds"): validate(d)
    def test_swap_does_not_count_as_ram(self):
        d = valid()
        d["samples"][1]["swap_written_bytes"] = 10**12
        self.assertTrue(validate(d)["valid"])
    def test_two_vcpu_rejected(self):
        d = valid();d["configuration"]["vcpus"] = 2
        with self.assertRaises(Invalid): validate(d)
    def test_non_integer_boolean_rejected(self):
        d = valid();d["samples"][1]["resident"]["game"] = True
        with self.assertRaises(Invalid): validate(d)
    def test_unknown_ram_category_rejected(self):
        d = valid();d["samples"][1]["resident"]["host_gpu"] = 0
        with self.assertRaises(Invalid): validate(d)
    def test_missing_component_rejected(self):
        d = valid();del d["samples"][1]["resident"]["kernel"]
        with self.assertRaises(Invalid): validate(d)
    def test_non_monotonic_time_rejected(self):
        d = valid();d["samples"][1]["t_ms"] = 0
        with self.assertRaises(Invalid): validate(d)
    def test_non_monotonic_faults_rejected(self):
        d = valid();d["samples"][0]["page_faults"] = 3
        with self.assertRaises(Invalid): validate(d)
    def test_negative_memory_rejected(self):
        d = valid();d["samples"][1]["resident"]["game"] = -1
        with self.assertRaises(Invalid): validate(d)
    def test_integrated_gpu_profile_separate(self):
        d = valid();d["configuration"]["graphics_backend"] = "integrated-gpu"
        self.assertEqual(validate(d)["profile"], "integrated-gpu")
    def test_unauthorized_swap_amount_rejected(self):
        d = valid();d["configuration"]["authorized_swap_bytes"] = -1
        with self.assertRaises(Invalid): validate(d)
    def test_missing_provenance_rejected(self):
        d = valid();del d["provenance"]["source"]
        with self.assertRaises(Invalid): validate(d)
    def test_missing_sample_rejected(self):
        d = valid();d["samples"] = []
        with self.assertRaises(Invalid): validate(d)
    def test_huge_guest_ram_rejected(self):
        d = valid();d["configuration"]["guest_ram_bytes"] = LIMIT + 1
        with self.assertRaises(Invalid): validate(d)
    def test_unknown_provenance_kind_rejected(self):
        d = valid();d["provenance"]["telemetry_kind"] = "verified-benchmark"
        with self.assertRaises(Invalid): validate(d)

if __name__ == "__main__":
    unittest.main()
