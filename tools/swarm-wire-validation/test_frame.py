import unittest
from frame import encode, decode, FrameError


class Frames(unittest.TestCase):
    def test_roundtrip(self):
        for kind in ("hello", "offer", "accept", "result", "cancel", "goodbye"):
            self.assertEqual(decode(encode(kind, "node_1", "job-2", "data"))["kind"], kind)

    def test_limits(self):
        with self.assertRaises(FrameError):
            encode("offer", "n", "j", "x" * 49153)

    def test_truncation(self):
        f = encode("hello", "n", "j", "")
        for cut in range(len(f)):
            with self.assertRaises(FrameError):
                decode(f[:cut])

    def test_duplicate_fields(self):
        raw = b'{"version":1,"version":1,"kind":"hello","node":"n","job":"j","payload":""}'
        with self.assertRaises(FrameError):
            decode(len(raw).to_bytes(4, "big") + raw)

    def test_noncanonical(self):
        raw = b'{"kind":"hello","version":1,"node":"n","job":"j","payload":""}'
        with self.assertRaises(FrameError):
            decode(len(raw).to_bytes(4, "big") + raw)

    def test_invalid_unicode_payload(self):
        with self.assertRaises(FrameError):
            encode("offer", "n", "j", "\\ud800")

    def test_invalid_kind_type(self):
        with self.assertRaises(FrameError):
            encode([], "n", "j", "")

    def test_payload_type(self):
        with self.assertRaises(FrameError):
            encode("offer", "n", "j", {"unexpected": "object"})

    def test_invalid_ids(self):
        for node in ("", "../x", "a" * 65, "with space"):
            with self.assertRaises(FrameError):
                encode("hello", node, "j", "")


if __name__ == "__main__":
    unittest.main()
