import copy
import unittest
from validate import validate


BASE = {
    "kernel_mib": 32, "game_mib": 300, "personality_mib": 20,
    "graphics_mib": 96, "paging_mib": 32, "other_mib": 16,
    "gpu_shared_mib": 80, "gpu_shared_limit_mib": 512,
    "gpu_dedicated_mib": 0, "vcpus": 1
}


class BudgetTests(unittest.TestCase):
    def test_shared_memory_not_added_twice(self):
        result = validate(BASE)
        self.assertEqual(result["guest_resident_mib"], 496)
        self.assertEqual(result["guest_headroom_mib"], 16)

    def test_exact_limit(self):
        data = {**BASE, "other_mib": 32}
        self.assertEqual(validate(data)["guest_resident_mib"], 512)

    def assert_reject(self, **changes):
        data = {**BASE, **changes}
        with self.assertRaises(ValueError):
            validate(data)

    def test_overcommit(self):
        self.assert_reject(game_mib=400)

    def test_shared_gpu_outside_graphics(self):
        self.assert_reject(gpu_shared_mib=97)

    def test_gpu_above_ceiling(self):
        self.assert_reject(gpu_shared_mib=90, gpu_shared_limit_mib=64)

    def test_dedicated_vram_not_shared_profile(self):
        self.assert_reject(gpu_dedicated_mib=512)

    def test_multiple_vcpus(self):
        self.assert_reject(vcpus=2)

    def test_negative(self):
        self.assert_reject(game_mib=-1)

    def test_nonfinite(self):
        self.assert_reject(game_mib=float("nan"))

    def test_booleans(self):
        self.assert_reject(vcpus=True)

    def test_missing(self):
        data = copy.deepcopy(BASE)
        del data["paging_mib"]
        with self.assertRaisesRegex(ValueError, "missing required"):
            validate(data)

    def test_invalid_top_level(self):
        with self.assertRaises(ValueError):
            validate([])


if __name__ == "__main__":
    unittest.main()
