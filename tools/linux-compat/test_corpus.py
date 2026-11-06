import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus


def elf(etype=2, arch=62):
    """One executable PT_LOAD in the Warden Linux virtual window."""
    b = bytearray(64 + 56 + 32)
    b[:4] = b"\x7fELF"
    b[4:7] = b"\x02\x01\x01"
    struct.pack_into("<HHI", b, 16, etype, arch, 1)
    struct.pack_into("<QQ", b, 24, 0x400000, 64)
    struct.pack_into("<HHH", b, 52, 64, 56, 1)
    struct.pack_into("<IIQQQQQQ", b, 64, 1, 5, 120, 0x400000, 0,
                     32, 4096, 4096)
    return bytes(b)


class CorpusTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "a-good").write_bytes(elf())
        (self.root / "b-pie").write_bytes(elf(etype=3))
        (self.root / "c-other-arch").write_bytes(elf(arch=183))
        (self.root / "d-not-elf").write_text("hello")
        (self.root / "e-huge").write_bytes(elf() + b"x" * 1024)
        (self.root / "nested").mkdir()
        (self.root / "nested" / "z-good").write_bytes(elf())
        (self.root / "symlink-good").symlink_to(self.root / "a-good")
        (self.root / "symlink-dir").symlink_to(self.root / "nested", target_is_directory=True)

    def test_histogram_and_classifications(self):
        r = corpus.inventory([self.root], max_files=100, max_bytes=256)
        self.assertEqual(r["summary"], {
            "files_checked": 6, "non_elf": 1, "elf_candidates": 2,
            "elf_blocked": 2, "oversize": 1, "read_errors": 0})
        self.assertEqual(r["blockers"], {
            "requires_static_et_exec_not_pie": 1, "requires_x86_64": 1})
        self.assertEqual([Path(x["path"]).name for x in r["results"]],
                         ["a-good", "b-pie", "c-other-arch", "e-huge", "z-good"])
        self.assertIn("No Linux software was executed", r["disclaimer"])

    def test_deterministic(self):
        a = corpus.inventory([self.root], max_files=100, max_bytes=256)
        b = corpus.inventory([self.root], max_files=100, max_bytes=256)
        self.assertEqual(json.dumps(a, sort_keys=True), json.dumps(b, sort_keys=True))

    def test_limit(self):
        r = corpus.inventory([self.root], max_files=2)
        self.assertEqual(r["summary"]["files_checked"], 2)
        self.assertEqual(r["summary"]["elf_candidates"], 1)
        self.assertEqual(r["summary"]["elf_blocked"], 1)

    def test_native_window(self):
        r = corpus.inventory([self.root / "a-good"], target="native")
        self.assertEqual(r["summary"]["elf_blocked"], 1)
        self.assertIn("segment_outside_warden_window", r["blockers"])

    def test_invalid_config(self):
        for maxfiles, maxbytes in ((0, 128), (1, 63)):
            with self.assertRaises(ValueError):
                corpus.inventory([self.root], max_files=maxfiles, max_bytes=maxbytes)
        with self.assertRaises(ValueError):
            corpus.inventory([])

    def test_no_symlink_follow(self):
        r = corpus.inventory([self.root / "symlink-good"])
        self.assertEqual(r["summary"]["files_checked"], 0)
        r = corpus.inventory([self.root / "symlink-dir"])
        self.assertEqual(r["summary"]["files_checked"], 0)

    def test_explicit_files_sorted_and_deduplicated(self):
        paths = [self.root / "b-pie", self.root / "a-good", self.root / "a-good"]
        r = corpus.inventory(paths)
        self.assertEqual([Path(x["path"]).name for x in r["results"]],
                         ["a-good", "b-pie"])


if __name__ == "__main__":
    unittest.main()
