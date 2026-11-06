import copy
import unittest

from check import CAP, COMPONENTS, EvidenceError
from reconcile import reconcile


def fixture():
    events = [
        {"time_ns": 0, "op": "alloc", "frame_id": 4, "kind": "kernel"},
        {"time_ns": 10_000_000, "op": "alloc", "frame_id": 6, "kind": "game"},
        {"time_ns": 12_000_000, "op": "alloc", "frame_id": 7, "kind": "graphics"},
        {"time_ns": 18_000_000, "op": "free", "frame_id": 6, "kind": "game"},
    ]
    trace = {
        "schema_version": 1, "source": "warden_guest_physical_frame_events",
        "capture_started_empty": True, "page_bytes": 4096,
        "guest_physical_capacity_bytes": CAP, "events": events
    }

    def sample(t, by_kind):
        parts = {k: by_kind.get(k, 0) * 4096 for k in COMPONENTS}
        return {"time_ms": t, "components": parts, "total_bytes": sum(parts.values())}

    snapshots = {
        "schema_version": 1, "source": "warden_guest_physical", "complete": True,
        "vcpu_count": 1, "guest_physical_capacity_bytes": CAP,
        "host_peak_rss_bytes": 700_000_000, "peak_total_bytes": 3 * 4096,
        "peak_method": "physical_frame_event_high_water",
        "samples": [
            sample(0, {"kernel": 1}),
            sample(20, {"kernel": 1, "graphics": 1}),
        ]
    }
    return snapshots, trace


class ReconcileTests(unittest.TestCase):
    def bad(self, snap, trace):
        with self.assertRaises(EvidenceError):
            reconcile(snap, trace)

    def test_v2_initial_kernel_reservation_matches_snapshots(self):
        snap, trace = fixture()
        trace["schema_version"] = 2
        trace["capture_started_empty"] = False
        trace["capture_start_ns"] = 0
        trace["initial_frames"] = [{"frame_id": 10, "kind": "paging"}]
        for sample in snap["samples"]:
            sample["components"]["paging"] = 4096
            sample["total_bytes"] += 4096
        snap["peak_total_bytes"] += 4096
        result = reconcile(snap, trace)
        self.assertTrue(result["reports_consistent"])
        self.assertEqual(result["peak_tracked_frame_bytes"], 4 * 4096)

    def test_v2_unreported_reservation_fails(self):
        snap, trace = fixture()
        trace["schema_version"] = 2
        trace["capture_started_empty"] = False
        trace["capture_start_ns"] = 0
        trace["initial_frames"] = [{"frame_id": 10, "kind": "paging"}]
        self.bad(snap, trace)

    def test_v2_sample_before_capture_start_fails(self):
        snap, trace = fixture()
        trace["schema_version"] = 2
        trace["capture_started_empty"] = False
        trace["capture_start_ns"] = 1
        trace["initial_frames"] = [{"frame_id": 10, "kind": "paging"}]
        self.bad(snap, trace)

    def test_valid_full_trace(self):
        snap, trace = fixture()
        result = reconcile(snap, trace)
        self.assertEqual(result["samples_matched"], 2)
        self.assertEqual(result["peak_tracked_frame_bytes"], 3 * 4096)

    def test_component_sum_can_match_but_category_wrong(self):
        snap, trace = fixture()
        snap["samples"][1]["components"]["game"] = 4096
        snap["samples"][1]["components"]["graphics"] = 0
        self.bad(snap, trace)

    def test_reported_peak_disagrees(self):
        snap, trace = fixture()
        snap["peak_total_bytes"] = 2 * 4096
        self.bad(snap, trace)

    def test_wrong_physical_capacity(self):
        snap, trace = fixture()
        trace["guest_physical_capacity_bytes"] = CAP - 4096
        self.bad(snap, trace)

    def test_snapshot_missing_event(self):
        snap, trace = fixture()
        del trace["events"][2]
        self.bad(snap, trace)

    def test_event_after_last_sample(self):
        snap, trace = fixture()
        trace["events"].append(
            {"time_ns": 21_000_000, "op": "alloc", "frame_id": 8, "kind": "paging"})
        self.bad(snap, trace)

    def test_boundary_event_included(self):
        snap, trace = fixture()
        trace["events"][3]["time_ns"] = 20_000_000
        self.assertTrue(reconcile(snap, trace)["reports_consistent"])

    def test_sample_before_allocation_rejects(self):
        snap, trace = fixture()
        trace["events"][0]["time_ns"] = 1
        self.bad(snap, trace)

    def test_actual_sample_max_can_be_lower_than_event_peak(self):
        snap, trace = fixture()
        self.assertEqual(max(s["total_bytes"] for s in snap["samples"]), 8192)
        self.assertEqual(snap["peak_total_bytes"], 12288)
        self.assertTrue(reconcile(snap, trace)["reports_consistent"])

    def test_double_allocation_rejected_by_replay(self):
        snap, trace = fixture()
        trace["events"][1]["frame_id"] = 4
        self.bad(snap, trace)


if __name__ == "__main__":
    unittest.main()
