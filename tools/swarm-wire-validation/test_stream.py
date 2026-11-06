import unittest
from frame import encode, FrameError, MAX_FRAME
from stream import FrameReader


class StreamTests(unittest.TestCase):
    def test_one_byte_chunks(self):
        data = encode("hello", "a", "j", "payload")
        reader = FrameReader()
        got = []
        for byte in data:
            got.extend(reader.feed(bytes([byte])))
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0]["payload"], "payload")

    def test_coalesced_frames(self):
        reader = FrameReader()
        data = encode("hello", "a", "j", "") + encode("result", "b", "j", "ok")
        self.assertEqual([x["kind"] for x in reader.feed(data)], ["hello", "result"])

    def test_oversized_prefix_fails_early(self):
        reader = FrameReader()
        with self.assertRaises(FrameError):
            reader.feed((MAX_FRAME + 1).to_bytes(4, "big"))
        self.assertEqual(reader.feed(encode("hello", "a", "j", ""))[0]["kind"], "hello")

    def test_rejects_invalid_frame(self):
        reader = FrameReader()
        with self.assertRaises(FrameError):
            reader.feed((1).to_bytes(4, "big") + b"x")

    def test_reset_discards_partial(self):
        reader = FrameReader()
        frame = encode("hello", "a", "j", "")
        self.assertEqual(reader.feed(frame[:5]), [])
        reader.reset()
        self.assertEqual(len(reader.feed(frame)), 1)

    def test_empty_feed(self):
        self.assertEqual(FrameReader().feed(b""), [])

    def test_non_bytes(self):
        with self.assertRaises(FrameError):
            FrameReader().feed("not bytes")


if __name__ == "__main__":
    unittest.main()
