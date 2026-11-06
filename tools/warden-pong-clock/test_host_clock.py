import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("host_clock", Path(__file__).with_name("host_clock.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

def fixture(a=180, b=300, c=420, missed=0, ticks=724):
    return (f"pong: ready\n"
            f"report pong 40 {a}\nreport pong 41 {b}\n"
            f"report pong 42 {c}\nreport pong 43 {missed}\n"
            f"pong: screen hash 0xab, kernel heap carved 0 more bytes, "
            f"503 frames (2012 KiB) of physical memory in use, {ticks} timer ticks\n"
            f"ALL HELD: 15 checks, 0 failed\n")

class ClockContractTests(unittest.TestCase):
    def test_valid(self):
        self.assertTrue(mod.validate(fixture(), 7.25, .4)["clock_contract_accepted"])
    def test_host_clock_too_fast(self):
        with self.assertRaises(ValueError): mod.validate(fixture(), 2.0, .4)
    def test_host_clock_too_slow(self):
        with self.assertRaises(ValueError): mod.validate(fixture(), 18.0, .4)
    def test_physics_runs_at_render_fps(self):
        with self.assertRaises(ValueError):
            mod.validate(fixture(a=90,b=50886,c=52824), 7.2, .4)
    def test_reversed_steps(self):
        with self.assertRaises(ValueError): mod.validate(fixture(a=180,b=170,c=420), 7.2, .4)
    def test_missing_report(self):
        with self.assertRaises(ValueError): mod.validate(fixture().replace("report pong 42 420\n",""),7.2,.4)
    def test_failed_kernel(self):
        with self.assertRaises(ValueError): mod.validate(fixture().replace("ALL HELD","PONG RUN FAILED"),7.2,.4)
    def test_excessive_saturation(self):
        with self.assertRaises(ValueError): mod.validate(fixture(missed=100),7.2,.4)
    def test_duplicate_report(self):
        with self.assertRaises(ValueError):
            mod.validate(fixture()+"report pong 40 180\n",7.2,.4)

if __name__ == "__main__":
    unittest.main()
