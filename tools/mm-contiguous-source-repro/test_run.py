import unittest
from pathlib import Path
from run import extract_mm, MM, START

class ExtractionContract(unittest.TestCase):
    def test_actual_allocator_only(self):
        code=extract_mm(MM.read_text())
        self.assertIn('pub struct FrameAlloc',code)
        self.assertIn('pub fn alloc_contiguous(',code)
        self.assertNotIn('pub struct AddressSpace',code)
        self.assertIn(START,code)
    def test_missing_anchor_rejected(self):
        with self.assertRaises(ValueError):
            extract_mm('pub struct FrameAlloc {}')
    def test_duplicate_anchor_rejected(self):
        with self.assertRaises(ValueError):
            extract_mm(MM.read_text()+START)
    def test_not_a_mock_allocator(self):
        code=extract_mm(MM.read_text())
        self.assertIn('self.free.pop()',code)
        self.assertIn('self.used += 1',code)

if __name__ == "__main__":
    unittest.main()
