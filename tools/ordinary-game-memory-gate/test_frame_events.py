import copy
import unittest
from frame_events import replay
from check import EvidenceError

CAP = 512 * 1024 * 1024


def fixture():
    return {
        "schema_version": 1,
        "source": "warden_guest_physical_frame_events",
        "capture_started_empty": True,
        "page_bytes": 4096,
        "guest_physical_capacity_bytes": CAP,
        "events": [
            {"time_ns": 1, "op": "alloc", "frame_id": 3, "kind": "kernel"},
            {"time_ns": 2, "op": "alloc", "frame_id": 4, "kind": "game"},
            {"time_ns": 3, "op": "free", "frame_id": 3, "kind": "kernel"},
            {"time_ns": 4, "op": "free", "frame_id": 4, "kind": "game"}
        ]
    }


class FrameEventTests(unittest.TestCase):
    def bad(self, x):
        with self.assertRaises(EvidenceError):
            replay(x)

    def test_full_512m_frame_population_and_release(self):
        # 131072 distinct 4-KiB guest frames require MORE than 100000 events.
        # Include releases to demonstrate a complete lifecycle can be replayed.
        pages = CAP // 4096
        x = fixture()
        x["events"] = (
            [{"time_ns": index, "op": "alloc", "frame_id": index, "kind": "game"}
             for index in range(pages)] +
            [{"time_ns": pages + index, "op": "free", "frame_id": index, "kind": "game"}
             for index in range(pages)]
        )
        out = replay(x)
        self.assertEqual(out["peak_tracked_frame_bytes"], CAP)
        self.assertEqual(out["end_tracked_frame_bytes"], 0)
        self.assertEqual(out["event_count"], 2 * pages)

    def test_excessive_event_count_fails_closed(self):
        x = fixture()
        x["events"] = [x["events"][0]] * 300001
        self.bad(x)

    def test_v2_initial_reservations_and_releases(self):
        x = fixture()
        x["schema_version"] = 2
        x["capture_started_empty"] = False
        x["capture_start_ns"] = 0
        x["initial_frames"] = [
            {"frame_id": 100, "kind": "kernel"},
            {"frame_id": 101, "kind": "paging"}
        ]
        x["events"].append(
            {"time_ns": 5, "op": "free", "frame_id": 101, "kind": "paging"})
        out = replay(x)
        self.assertEqual(out["initial_frame_count"], 2)
        self.assertEqual(out["peak_tracked_frame_bytes"], 4 * 4096)
        self.assertEqual(out["end_tracked_frame_bytes"], 4096)

    def test_v2_rejects_duplicate_initial_frame(self):
        x = fixture()
        x["schema_version"] = 2
        x["capture_started_empty"] = False
        x["capture_start_ns"] = 0
        x["initial_frames"] = [
            {"frame_id": 100, "kind": "kernel"},
            {"frame_id": 100, "kind": "game"}
        ]
        self.bad(x)

    def test_v2_rejects_alloc_of_reserved_frame(self):
        x = fixture()
        x["schema_version"] = 2
        x["capture_started_empty"] = False
        x["capture_start_ns"] = 0
        x["initial_frames"] = [{"frame_id": 3, "kind": "kernel"}]
        self.bad(x)

    def test_v2_rejects_event_before_capture_start(self):
        x = fixture()
        x["schema_version"] = 2
        x["capture_started_empty"] = False
        x["capture_start_ns"] = 2
        x["initial_frames"] = [{"frame_id": 100, "kind": "kernel"}]
        self.bad(x)

    def test_v2_rejects_unbounded_initial_frame(self):
        x = fixture()
        x["schema_version"] = 2
        x["capture_started_empty"] = False
        x["capture_start_ns"] = 0
        x["initial_frames"] = [{"frame_id": CAP // 4096, "kind": "kernel"}]
        self.bad(x)

    def test_v1_rejects_initial_inventory(self):
        x = fixture()
        x["initial_frames"] = [{"frame_id": 100, "kind": "kernel"}]
        self.bad(x)

    def test_peak_and_end(self):
        out = replay(fixture())
        self.assertEqual(out["peak_tracked_frame_bytes"], 8192)
        self.assertEqual(out["end_tracked_frame_bytes"], 0)

    def test_duplicate_allocation(self):
        x = fixture()
        x["events"][1]["frame_id"] = 3
        self.bad(x)

    def test_stale_or_double_free(self):
        x = fixture()
        x["events"].append({"time_ns": 5, "op": "free", "frame_id": 4, "kind": "game"})
        self.bad(x)

    def test_wrong_owner_released(self):
        x = fixture()
        x["events"][2]["kind"] = "game"
        self.bad(x)

    def test_untracked_frame(self):
        x = fixture()
        x["events"][0]["frame_id"] = CAP // 4096
        self.bad(x)

    def test_timestamp_reversal(self):
        x = fixture()
        x["events"][2]["time_ns"] = 0
        self.bad(x)

    def test_negative_boolean_and_invalid_kind(self):
        for field, value in (("frame_id", -1), ("frame_id", True), ("kind", "host_rss")):
            x = fixture()
            x["events"][0][field] = value
            self.bad(x)

    def test_ambiguous_capture_and_source(self):
        x = fixture()
        x["capture_started_empty"] = False
        self.bad(x)
        x = fixture()
        x["source"] = "linux_process_rss"
        self.bad(x)

    def test_event_bounds(self):
        x = fixture()
        x["events"] = []
        self.bad(x)
        x = fixture()
        x["guest_physical_capacity_bytes"] = CAP + 4096
        self.bad(x)

    def test_frame_reuse_after_valid_free(self):
        x = fixture()
        x["events"].append({"time_ns": 5, "op": "alloc", "frame_id": 3, "kind": "paging"})
        out = replay(x)
        self.assertEqual(out["end_tracked_frame_bytes"], 4096)
        self.assertEqual(out["peak_tracked_frame_bytes"], 8192)


if __name__ == "__main__":
    unittest.main()
