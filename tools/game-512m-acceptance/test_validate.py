import copy
import unittest
from validate import LIMIT, COMPONENTS, check

def fixture(verified=False):
    return {
        "schema": 1, "evidence": "native_warden_qemu" if verified else "model",
        "claim": "guest_verified" if verified else "model_only",
        "warden_commit": "a" * 40, "game_sha256": "b" * 64,
        "guest": {"physical_ram_bytes": LIMIT, "vcpus": 1,
                  "graphics": "software", "storage_authorized": True,
                  "memory_components_bytes": {k: 0 for k in COMPONENTS}},
        "runs": ([{"boot_id": str(i), "boot_ok": True, "exit_ok": True,
                   "input_and_state_advanced": True,
                   "frames": [{"timestamp_ns": j+1, "digest": "a" * 16} for j in range(300)]}
                  for i in range(2)] if verified else [])
    }

class AcceptanceTests(unittest.TestCase):
    def test_exact_limit(self):
        self.assertEqual(check(fixture()), [])
    def test_one_extra_page(self):
        x = fixture(); x["guest"]["physical_ram_bytes"] += 4096
        self.assertIn("guest physical RAM", " ".join(check(x)))
    def test_one_core_only(self):
        x = fixture(); x["guest"]["vcpus"] = 2
        self.assertIn("one guest vCPU", " ".join(check(x)))
    def test_missing_accounting(self):
        x = fixture(); del x["guest"]["memory_components_bytes"]["kernel"]
        self.assertIn("memory accounting", " ".join(check(x)))
    def test_accounting_exceeds_ram(self):
        x = fixture(); x["guest"]["memory_components_bytes"]["game_resident"] = LIMIT + 1
        self.assertIn("exceed", " ".join(check(x)))
    def test_authorized_storage(self):
        x = fixture(); x["guest"]["storage_authorized"] = False
        self.assertIn("authorized storage", " ".join(check(x)))
    def test_model_cannot_claim_verified(self):
        x = fixture(); x["claim"] = "guest_verified"
        self.assertIn("model data", " ".join(check(x)))
    def test_two_boots_300_frames(self):
        self.assertEqual(check(fixture(True)), [])
    def test_missing_second_boot(self):
        x = fixture(True); x["runs"].pop()
        self.assertIn("two independent", " ".join(check(x)))
    def test_bad_frame_digest(self):
        x = fixture(True); x["runs"][0]["frames"][0]["digest"] = "zz"
        self.assertIn("invalid frame", " ".join(check(x)))
    def test_duplicate_boot(self):
        x = fixture(True); x["runs"][1]["boot_id"] = "0"
        self.assertIn("duplicate boot", " ".join(check(x)))
    def test_non_monotone_time(self):
        x = fixture(True); x["runs"][0]["frames"][1]["timestamp_ns"] = 1
        self.assertIn("timestamp", " ".join(check(x)))
    def test_bool_is_not_int(self):
        x = fixture(); x["guest"]["physical_ram_bytes"] = True
        self.assertIn("physical RAM", " ".join(check(x)))

if __name__ == "__main__":
    unittest.main()
