#!/usr/bin/env python3
import copy
import json
import unittest
import tempfile
from pathlib import Path
from check import main
from check import Invalid, validate, unique_pairs, reject_constant

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

    def test_duplicate_key_rejected(self):
        with self.assertRaises(Invalid):
            json.loads('{"a":1,"a":2}', object_pairs_hook=unique_pairs)

    def test_nonfinite_json_rejected(self):
        with self.assertRaises(Invalid):
            json.loads('{"value":NaN}', parse_constant=reject_constant)

    def test_boolean_schema_rejected(self):
        self.reject(lambda d: d.update(schema=True))

    def test_strict_single_pid_accepts_one_game(self):
        result = validate(copy.deepcopy(BASE), strict_single_pid=True)
        self.assertTrue(result["strict_single_pid"])
        self.assertEqual(result["peak_guest_userspace_processes"], 1)

    def test_strict_single_pid_rejects_sequential_helper(self):
        doc = copy.deepcopy(BASE)
        doc["events"] = [
            {"seq":0,"event":"boot"},
            {"seq":1,"event":"spawn","pid":2,"role":"helper"},
            {"seq":2,"event":"exit","pid":2,"role":"helper"},
            {"seq":3,"event":"spawn","pid":5,"role":"game"},
            {"seq":4,"event":"exit","pid":5,"role":"game"},
            {"seq":5,"event":"end"}]
        self.assertEqual(validate(doc)["peak_guest_userspace_processes"], 1)
        with self.assertRaises(Invalid):
            validate(doc, strict_single_pid=True)

    def test_strict_flag_must_be_bool(self):
        with self.assertRaises(Invalid):
            validate(copy.deepcopy(BASE), strict_single_pid=1)

    def test_cli_strict_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "report.json"
            path.write_text(json.dumps(BASE), encoding="utf-8")
            self.assertEqual(main(["check.py", str(path), "--strict-single-pid"]), 0)
            self.assertEqual(main(["check.py", str(path), "--unexpected"]), 2)
            self.assertEqual(main(["check.py", str(path), "--strict-single-pid", "--unexpected"]), 2)

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
