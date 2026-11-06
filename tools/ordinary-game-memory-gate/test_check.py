import unittest
import json
import tempfile
from pathlib import Path
from check import CAP, COMPONENTS, EvidenceError, validate, load_bounded


def sample(time_ms, game=1000000):
    components = {k: 0 for k in COMPONENTS}
    components.update(kernel=3000000, runtime=2000000, game=game,
                      framebuffer=307200, paging=200000, graphics=100000,
                      other=50000)
    return {"time_ms": time_ms, "components": components,
            "total_bytes": sum(components.values())}


def fixture():
    samples = [sample(0), sample(16, 2000000)]
    return {"schema_version": 1, "source": "warden_guest_physical",
            "complete": True, "vcpu_count": 1,
            "guest_physical_capacity_bytes": CAP,
            "host_peak_rss_bytes": 900000000,
            "peak_total_bytes": max(s["total_bytes"] for s in samples) + 1000000,
            "peak_method": "physical_frame_event_high_water",
            "samples": samples}


class GateTests(unittest.TestCase):
    def bad(self, f):
        with self.assertRaises(EvidenceError):
            validate(f)

    def test_bounded_file_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "evidence.json"
            p.write_text(json.dumps(fixture()), encoding="utf-8")
            self.assertTrue(validate(load_bounded(p))["accepted"])

    def test_duplicate_keys_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "evidence.json"
            p.write_text('{"vcpu_count":1,"vcpu_count":2}', encoding="utf-8")
            with self.assertRaises(EvidenceError):
                load_bounded(p)

    def test_oversized_report_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "huge.json"
            p.write_bytes(b" " * (4 * 1024 * 1024 + 1))
            with self.assertRaises(EvidenceError):
                load_bounded(p)

    def test_nested_duplicate_keys_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "nested.json"
            p.write_text('{"a":{"b":1,"b":0}}', encoding="utf-8")
            with self.assertRaises(EvidenceError):
                load_bounded(p)

    def test_good(self):
        result = validate(fixture())
        self.assertTrue(result["accepted"])
        self.assertEqual(result["samples"], 2)

    def test_missing_component(self):
        f = fixture()
        del f["samples"][0]["components"]["paging"]
        self.bad(f)

    def test_only_process_memory_fails(self):
        f = fixture()
        f["source"] = "game_process_rss"
        self.bad(f)

    def test_cap_and_vcpu(self):
        f = fixture()
        f["vcpu_count"] = 2
        self.bad(f)
        f = fixture()
        f["guest_physical_capacity_bytes"] = CAP + 1
        self.bad(f)

    def test_over_budget_sample(self):
        f = fixture()
        f["samples"][1] = sample(16, CAP)
        f["peak_total_bytes"] = f["samples"][1]["total_bytes"]
        self.bad(f)

    def test_bad_totals(self):
        f = fixture()
        f["samples"][0]["total_bytes"] += 1
        self.bad(f)
        f = fixture()
        f["peak_total_bytes"] = 1
        self.bad(f)

    def test_spike_missing_from_samples_but_exceeding_capacity(self):
        f = fixture()
        f["peak_total_bytes"] = CAP + 1
        self.bad(f)

    def test_peak_method_required(self):
        f = fixture()
        f["peak_method"] = "sample_max"
        self.bad(f)
        f = fixture()
        del f["peak_method"]
        self.bad(f)

    def test_higher_event_peak_is_valid(self):
        f = fixture()
        f["peak_total_bytes"] = 100000000
        out = validate(f)
        self.assertGreater(out["event_peak_mib"], out["sampled_peak_mib"])

    def test_incomplete_and_bad_timestamps(self):
        f = fixture()
        f["complete"] = False
        self.bad(f)
        f = fixture()
        f["samples"][1]["time_ms"] = 0
        self.bad(f)

    def test_boolean_integer_and_unknown_field(self):
        f = fixture()
        f["samples"][0]["components"]["game"] = True
        self.bad(f)
        f = fixture()
        f["extra"] = 3
        self.bad(f)

    def test_malformed_length(self):
        f = fixture()
        f["samples"] = f["samples"][:1]
        self.bad(f)
        f = fixture()
        f["samples"][1]["components"]["kernel"] = -5
        self.bad(f)


if __name__ == "__main__":
    unittest.main()
