import unittest
from swap_probe import Pager, SwapFault, PAGE_SIZE, bench


class PagerTests(unittest.TestCase):
    def test_zero_new_page(self):
        with Pager(1, 1) as p:
            p.alloc(0)
            self.assertEqual(p.read(0), bytes(PAGE_SIZE))

    def test_page_roundtrip_through_eviction(self):
        with Pager(1, 2) as p:
            p.alloc(0)
            p.write(0, b'A' * PAGE_SIZE)
            p.alloc(1)
            self.assertEqual(p.read(0), b'A' * PAGE_SIZE)

    def test_capacity_exhausted(self):
        with Pager(1, 1) as p:
            p.alloc(0)
            p.alloc(1)
            with self.assertRaises(SwapFault):
                p.alloc(2)
            self.assertEqual(p.stats()['allocated'], 2)

    def test_repeated_dirty_evictions(self):
        with Pager(1, 2) as p:
            for i in range(3):
                p.alloc(i)
                p.write(i, bytes([i]) * PAGE_SIZE)
            for _ in range(3):
                for i in range(3):
                    self.assertEqual(p.read(i), bytes([i]) * PAGE_SIZE)

    def test_corrupted_slot_rejected(self):
        with Pager(1, 2) as p:
            p.alloc(0)
            p.write(0, b'B' * PAGE_SIZE)
            p.alloc(1)
            slot, _ = p._slots[0]
            p._file.seek(slot * PAGE_SIZE)
            p._file.write(b'Z')
            p._file.flush()
            with self.assertRaisesRegex(SwapFault, 'corrupt'):
                p.read(0)

    def test_invalid_write(self):
        with Pager(1, 1) as p:
            p.alloc(0)
            with self.assertRaises(ValueError):
                p.write(0, b'bad')

    def test_duplicate(self):
        with Pager(1, 1) as p:
            p.alloc(0)
            with self.assertRaises(SwapFault):
                p.alloc(0)

    def test_free_reuses_slot(self):
        with Pager(1, 1) as p:
            p.alloc(0)
            p.alloc(1)
            p.free(0)
            p.alloc(2)
            self.assertEqual(p.read(2), bytes(PAGE_SIZE))

    def test_close(self):
        p = Pager(1, 1)
        p.alloc(0)
        p.close()
        with self.assertRaises(SwapFault):
            p.read(0)

    def test_invalid_config(self):
        with self.assertRaises(ValueError):
            Pager(0, 1)

    def test_stats_bound(self):
        with Pager(2, 2) as p:
            for i in range(4):
                p.alloc(i)
            self.assertLessEqual(p.stats()['resident_bytes'], 2 * PAGE_SIZE)

    def test_benchmark(self):
        r = bench(8, 2, 2)
        self.assertEqual(r['allocated'], 8)
        self.assertTrue(r['host_only'])


if __name__ == '__main__':
    unittest.main()
