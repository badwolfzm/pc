"""Prevent guest count growth from exceeding endpoint queue capacity."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
APPS = ROOT / "crates/warden-kernel/src/apps.rs"

class QueueCapacity(unittest.TestCase):
    def test_personality_queue_bounded_by_capability_ceiling(self):
        text = APPS.read_text(encoding="utf-8")
        self.assertRegex(text, r"let ep_capacity\s*=\s*\(n_clients as u64\)\.min\(warden_caps::ENDPOINT_MAX_QUEUE\)")
        self.assertIn("thread::create_endpoint(k, srv, SLOT_UNTYPED, SLOT_SCRATCH, ep_capacity)", text)

    def test_no_client_plus_server_overallocation(self):
        text = APPS.read_text(encoding="utf-8")
        self.assertNotIn("let ep_capacity = (n_clients + 1) as u64", text)

if __name__ == "__main__":
    unittest.main()
