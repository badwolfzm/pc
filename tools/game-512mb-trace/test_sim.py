import tempfile
import unittest
from pathlib import Path
from sim import capacity,parse_trace,simulate
class SimTest(unittest.TestCase):
    def test_capacity_counts_guest_overhead(self):
        self.assertEqual(capacity(512,192,4096,0),81920)
        self.assertLess(capacity(512,192,4096,64),81920)
        with self.assertRaises(ValueError): capacity(512,512,4096,64)
    def test_belady(self):
        seq=[7,0,1,2,0,3,0,4,2,3,0,3,2]
        rows=[(x,False) for x in seq]
        a={p:simulate(rows,3,p,100,200) for p in ('fifo','lru','opt')}
        self.assertEqual(a['opt']['faults'],7)
        self.assertEqual(a['fifo']['faults'],10)
        self.assertEqual(a['lru']['faults'],9)
    def test_dirty_writeback(self):
        x=simulate([(1,True),(2,False),(3,False),(1,False)],1,'fifo',100,200)
        self.assertEqual((x['faults'],x['dirty_writebacks'],x['estimated_io_stall_us']),(4,1,600))
    def test_zero_length_and_large_capacity(self):
        x=simulate([(1,False)]*5,10,'lru',10,20)
        self.assertEqual((x['hits'],x['faults']),(4,1))
    def test_trace_validation(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'trace.csv'
            p.write_text('page,op\n1,R\n0x20,W\n',encoding='utf-8')
            self.assertEqual(parse_trace(p),[(1,False),(32,True)])
            for bad in ('page,op\n-1,R\n','page,op\n1,X\n','page,op\n','bad,op\n1,R\n','page,op\n0xZZ,R\n'):
                p.write_text(bad,encoding='utf-8')
                with self.assertRaises(ValueError): parse_trace(p)
if __name__=='__main__':unittest.main()
