#!/usr/bin/env python3
import copy
import unittest
from check import Invalid, validate

BASE = {"schema": 1, "source": "synthetic", "boot_id": "a" * 64,
        "complete": True, "vcpu_count": 1,
        "events": [{"seq": 0, "event": "boot"},
                   {"seq": 1, "event": "spawn", "pid": 5, "role": "game"},
                   {"seq": 2, "event": "exit", "pid": 5, "role": "game"},
                   {"seq": 3, "event": "end"}]}

class InventoryTests(unittest.TestCase):
    def test_success(self):
        self.assertEqual(validate(copy.deepcopy(BASE))["game_pid"], 5)

    def reject(self, mutation):
        doc = copy.deepcopy(BASE)
        mutation(doc)
        with self.assertRaises(Invalid):
            validate(doc)

    def test_rejections(self):
        mutations = [
            lambda d: d.update(complete=False),
            lambda d: d.update(vcpu_count=True),
            lambda d: d.update(vcpu_count=2),
            lambda d: d.update(source="native-verified"),
            lambda d: d.update(boot_id="invalid"),
            lambda d: d["events"][1].update(seq=2),
            lambda d: d["events"].pop(),
            lambda d: d["events"][2].update(pid=6),
            lambda d: d["events"][2].update(role="helper"),
            lambda d: d["events"][1].update(pid=True),
            lambda d: d["events"][1].update(role="helper"),
            lambda d: d["events"][1].update(extra="untrusted"),
            lambda d: d.update(unknown=123),
            lambda d: d["events"].insert(2, {"seq": 2, "event": "spawn", "pid": 6, "role": "helper"}),
            lambda d: d["events"][0].update(event="end"),
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutations.index(mutation)):
                self.reject(mutation)

    def test_sequential_helper_allowed(self):
        doc = copy.deepcopy(BASE)
        doc["events"] = [
            {"seq":0,"event":"boot"},
            {"seq":1,"event":"spawn","pid":2,"role":"helper"},
            {"seq":2,"event":"exit","pid":2,"role":"helper"},
            {"seq":3,"event":"spawn","pid":5,"role":"game"},
            {"seq":4,"event":"exit","pid":5,"role":"game"},
            {"seq":5,"event":"end"}]
        self.assertEqual(validate(doc)["peak_guest_userspace_processes"], 1)

if __name__ == "__main__":
    unittest.main()
