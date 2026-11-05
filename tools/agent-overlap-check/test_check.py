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
