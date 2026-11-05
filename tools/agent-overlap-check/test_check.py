"""Regression tests for the offline overlap checker."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("claim_checker", ROOT / "check.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ClaimTests(unittest.TestCase):
    def check(self, agents):
        return checker.find_collisions({"agents": agents})

    def test_exact_file_collision(self):
        self.assertEqual(len(self.check([
            {"id": "a", "paths": ["kernel/a.rs"]},
            {"id": "b", "paths": ["kernel/a.rs"]},
        ])), 1)

    def test_directory_contains_file(self):
        self.assertEqual(len(self.check([
            {"id": "a", "paths": ["crates/kernel/**"]},
            {"id": "b", "paths": ["crates/kernel/src/lib.rs"]},
        ])), 1)

    def test_nested_directory(self):
        self.assertEqual(len(self.check([
            {"id": "a", "paths": ["matrixos/**"]},
            {"id": "b", "paths": ["matrixos/assets/**"]},
        ])), 1)

    def test_sibling_directory_no_collision(self):
        self.assertEqual(self.check([
            {"id": "a", "paths": ["matrixos/assets/**"]},
            {"id": "b", "paths": ["matrixos/canvas/**"]},
        ]), [])

    def test_prefix_not_substring(self):
        self.assertEqual(self.check([
            {"id": "a", "paths": ["src/net/**"]},
            {"id": "b", "paths": ["src/network/file.c"]},
        ]), [])

    def test_same_agent_no_collision(self):
        self.assertEqual(self.check([
            {"id": "a", "paths": ["src/**", "src/lib.rs"]},
        ]), [])

    def test_duplicate_agent_rejected(self):
        with self.assertRaises(ValueError):
            self.check([{"id": "x", "paths": ["a"]}, {"id": "x", "paths": ["b"]}])

    def test_unsafe_or_ambiguous_patterns_rejected(self):
        for bad in ("/abs", "../x", "a/../x", "a//b", "a/*", "a/*.rs", "a/.", "", "a\\b", "**"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                checker.normalize(bad)

    def test_control_characters_rejected(self):
        for bad in ("a\nb", "a\rb", "a\x1bb", "a\x7fb", " a", "a "):
            with self.subTest(bad=repr(bad)), self.assertRaises(ValueError):
                checker.normalize(bad)
        for bad in ("a\nb", "a\x1b[31m", " a", "a "):
            with self.subTest(agent=repr(bad)), self.assertRaises(ValueError):
                self.check([{"id": bad, "paths": ["src/a.rs"]}])

    def test_non_string_paths_rejected(self):
        for bad in (None, 5, {}, ["a"]):
            with self.subTest(bad=repr(bad)), self.assertRaises(ValueError):
                checker.normalize(bad)

    def test_review_finding_nul_rejected_in_cli(self):
        # The independent review at dc3f47e found that an invalid NUL path
        # could previously produce a misleading exit-0 "no overlap" result.
        for bad_field in ("path", "agent"):
            with self.subTest(field=bad_field), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "claims.json"
                agents = [
                    {"id": "a", "paths": ["src/real.rs"]},
                    {"id": "b", "paths": ["src/\x00hidden.rs"]},
                ]
                if bad_field == "agent":
                    agents[1]["paths"] = ["src/other.rs"]
                    agents[1]["id"] = "b\x00spoof"
                path.write_text(json.dumps({"agents": agents}), encoding="utf-8")
                run = subprocess.run([sys.executable, str(ROOT / "check.py"), str(path)],
                                     capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
                self.assertIn("INVALID CLAIMS", run.stderr)
                self.assertNotIn("0 overlap(s)", run.stdout)

    def test_file_directory_boundary_exact_match(self):
        self.assertEqual(len(self.check([
            {"id": "a", "paths": ["source"]},
            {"id": "b", "paths": ["source/**"]},
        ])), 1)
        self.assertEqual(self.check([
            {"id": "a", "paths": ["source"]},
            {"id": "b", "paths": ["source/file.py"]},
        ]), [])

    def test_cli_exit_codes(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "claims.json"
            for agents, expected in (
                ([{"id": "a", "paths": ["one"]}, {"id": "b", "paths": ["two"]}], 0),
                ([{"id": "a", "paths": ["one"]}, {"id": "b", "paths": ["one"]}], 1),
                ([{"id": "a", "paths": ["a/*"]}], 2),
            ):
                path.write_text(json.dumps({"agents": agents}), encoding="utf-8")
                run = subprocess.run([sys.executable, str(ROOT / "check.py"), str(path)],
                                     capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, expected, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
