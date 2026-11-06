"""Regression guard for bounded, lossless Linux guest death notifications.

These are structural guards; the QEMU guest suite is the integration test.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
THREAD = (ROOT / "crates/warden-kernel/src/thread.rs").read_text(encoding="utf-8")
LINUX = (ROOT / "crates/warden-kernel/src/linux.rs").read_text(encoding="utf-8")


class DeferredExitTests(unittest.TestCase):
    def test_fixed_size_per_thread_storage(self):
        self.assertIn("pub deferred_exit: Option<(u32, Msg)>", THREAD)
        self.assertIn("deferred_exit: None", THREAD)
        self.assertIn("t.deferred_exit = None", THREAD)

    def test_no_extra_endpoint_capacity(self):
        self.assertIn("t.death.is_some() && t.deferred_exit.map", THREAD)
        self.assertIn("k.threads[from].deferred_exit.take()", THREAD)
        self.assertIn("k.threads[t].deferred_exit = Some((ep_id, msg))", THREAD)
        self.assertIn("if !Endpoint::enqueue(&mut ep.senders, ep.capacity, t)", THREAD)
        self.assertNotIn("ENDPOINT_MAX_QUEUE + 1); // extend queue", THREAD)

    def test_queue_backpressure_is_not_conflated_with_missing_exits(self):
        main = (ROOT / "crates/warden-kernel/src/main.rs").read_text(encoding="utf-8")
        self.assertIn("k.ep_over_capacity == 0 && k.ep_refused >= 1", main)
        self.assertIn("CHECK 111 proves all ten", main)
        self.assertNotIn("k.ep_refused == 1", main)

    def test_linux_exit_remains_badged_capability_mediated(self):
        self.assertIn("k.caps.invoke(cs, SLOT_PERSONALITY, Rights::WRITE)", LINUX)
        self.assertIn("Msg { badge: ep.badge, w: [231, code, 0, 0] }", LINUX)
        self.assertIn("thread::notify(k, t, ep.object.0, msg)", LINUX)


if __name__ == "__main__":
    unittest.main()
