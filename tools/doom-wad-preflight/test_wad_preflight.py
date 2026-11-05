import struct
import tempfile
import unittest
from wad_preflight import inspect_wad, WadError, MAX_LUMPS, MAX_FILE_BYTES


def make_wad(magic=b'PWAD', entries=((12, 0, b'MAP01'),), directory=12):
    return (struct.pack('<4sII', magic, len(entries), directory) +
            b''.join(struct.pack('<II8s', off, size, name) for off, size, name in entries))


class WadPreflightTests(unittest.TestCase):
    def check(self, data):
        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(data)
            tmp.flush()
            return inspect_wad(tmp.name)

    def rejects(self, data):
        with self.assertRaises(WadError):
            self.check(data)

    def test_valid_pwad(self):
        r = self.check(make_wad())
        self.assertEqual((r['format'], r['lumps'], r['map_markers']), ('PWAD', 1, ['MAP01']))

    def test_valid_iwad(self):
        self.assertEqual(self.check(make_wad(magic=b'IWAD'))['format'], 'IWAD')

    def test_truncated_header(self):
        self.rejects(b'PWAD\x01')

    def test_bad_magic(self):
        self.rejects(make_wad(magic=b'XXXX'))

    def test_directory_overflow(self):
        self.rejects(struct.pack('<4sII', b'PWAD', 2, 0xffffffff))

    def test_excessive_lumps(self):
        self.rejects(struct.pack('<4sII', b'PWAD', MAX_LUMPS + 1, 12))

    def test_out_of_range_lump(self):
        self.rejects(make_wad(entries=((0xffffffff, 12, b'MAP01'),)))

    def test_invalid_name(self):
        self.rejects(make_wad(entries=((12, 0, b'\xffTEST'),)))

    def test_empty_name(self):
        self.rejects(make_wad(entries=((12, 0, b''),)))

    def test_empty_wad(self):
        self.assertEqual(self.check(struct.pack('<4sII', b'PWAD', 0, 12))['lumps'], 0)

    def test_large_sparse_file_rejected(self):
        with tempfile.NamedTemporaryFile() as tmp:
            tmp.truncate(MAX_FILE_BYTES + 1)
            with self.assertRaises(WadError):
                inspect_wad(tmp.name)

    def test_oversized_count_with_short_file(self):
        self.rejects(struct.pack('<4sII', b'PWAD', MAX_LUMPS, 12))


if __name__ == '__main__':
    unittest.main()
