#!/usr/bin/env python3
import unittest
from qmp_bridge import ppm_to_rgba, KEYS


class Tests(unittest.TestCase):
    def test_two_by_two_scaled(self):
        # Red, green / blue, white; confirm nearest-neighbor positions.
        src = b'P6\n2 2\n255\n' + bytes([255,0,0, 0,255,0, 0,0,255, 255,255,255])
        rgba = ppm_to_rgba(src)
        self.assertEqual(len(rgba), 320*200*4)
        self.assertEqual(rgba[:4], bytes([255,0,0,255]))
        self.assertEqual(rgba[(319)*4:(320)*4], bytes([0,255,0,255]))
        self.assertEqual(rgba[(199*320)*4:(199*320+1)*4], bytes([0,0,255,255]))
        self.assertEqual(rgba[-4:], bytes([255,255,255,255]))

    def test_binary_data_may_start_with_whitespace(self):
        src = b'P6\n1 1\n255\n' + bytes([10,32,35])
        self.assertEqual(ppm_to_rgba(src)[:4], bytes([10,32,35,255]))

    def test_reject_malformed(self):
        for raw in (b'P3\n1 1\n255\n000', b'P6\n0 2\n255\n',
                    b'P6\n1 1\n100\n\x00\x00\x00', b'P6\n1 1\n255\n\x00'):
            with self.subTest(raw=raw):
                with self.assertRaises(ValueError):
                    ppm_to_rgba(raw)

    def test_input_key_allowlist(self):
        self.assertEqual(KEYS['ArrowUp'],'up')
        self.assertEqual(KEYS['KeyW'],'w')
        self.assertNotIn('MetaLeft',KEYS)


if __name__ == '__main__':
    unittest.main()
