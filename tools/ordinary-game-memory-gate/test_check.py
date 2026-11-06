import copy
import unittest
from check import CAP, COMPONENTS, EvidenceError, validate


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
            "peak_total_bytes": max(s["total_bytes"] for s in samples),
            "samples": samples}


class GateTests(unittest.TestCase):
    def bad(self, f):
        with self.assertRaises(EvidenceError):
            validate(f)

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
        f["peak_total_bytes"] += 1
        self.bad(f)

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
