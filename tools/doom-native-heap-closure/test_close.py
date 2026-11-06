import importlib.util
from pathlib import Path
import unittest

source = Path(__file__).with_name("close.py")
spec = importlib.util.spec_from_file_location("doom_heap_closure", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class HeapClosureTests(unittest.TestCase):
    def test_all_four_removed_with_other_imports_unchanged(self):
        self.assertEqual(
            module.closure(["malloc", "free", "calloc", "realloc", "fopen"],
                           ["fopen"]),
            ["calloc", "free", "malloc", "realloc"])

    def test_incomplete_baseline_fails(self):
        with self.assertRaises(ValueError):
            module.closure(["malloc"], [])

    def test_unresolved_heap_fails(self):
        with self.assertRaises(ValueError):
            module.closure(sorted(module.ALLOCS), ["realloc"])

    def test_cannot_silently_add_external_imports(self):
        with self.assertRaises(ValueError):
            module.closure(sorted(module.ALLOCS), ["system"])

    def test_fixed_dependency_digest(self):
        self.assertEqual(len(module.ARENA_SHA), 40)
        self.assertEqual(module.ALLOCS, {"malloc", "free", "calloc", "realloc"})

if __name__ == "__main__":
    unittest.main()
