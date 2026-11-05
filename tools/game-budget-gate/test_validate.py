#!/usr/bin/env python3
"""Self-contained stdlib regression suite; no game or QEMU is executed."""
import copy
import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).with_name("validate.py")
spec = importlib.util.spec_from_file_location("game_budget_validator", path)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

def fixture():
    def run():
        return dict(boot_ok=True, clean_exit=True, warmup_frames=30,
                    timed_frames=300, frametimes_ms=[50.0]*300,
                    frame_digest="0123456789abcdef",
                    guest_peak_bytes=32*1024*1024,
                    host_peak_rss_bytes=128*1024*1024)
    return dict(target=dict(platform="warden-qemu", game_processes=1),
                qemu=dict(vcpus=1, mode="tcg", cpu="qemu64", version="8.2"),
                limits=dict(guest_ram_cap_bytes=64*1024*1024,
                            host_rss_cap_bytes=256*1024*1024),
                runs=[run(),run()])

class EvidenceTests(unittest.TestCase):
    def test_positive_fixture(self):
        self.assertEqual(validator.validate(fixture()), [])
    def test_reject_inconsistent_measurements(self):
        mutations = [
            lambda d: d["qemu"].update(vcpus=2),
            lambda d: d["qemu"].update(vcpus=True),
            lambda d: d["target"].update(game_processes=2),
            lambda d: d["limits"].update(guest_ram_cap_bytes=1),
            lambda d: d["runs"][0].update(host_peak_rss_bytes=300*1024*1024),
            lambda d: d["runs"][0].update(boot_ok=False),
            lambda d: d["runs"][0].update(clean_exit=False),
            lambda d: d["runs"][0].update(timed_frames=299,frametimes_ms=[50.0]*299),
            lambda d: d["runs"][0]["frametimes_ms"].__setitem__(0, float("nan")),
            lambda d: d["runs"][0]["frametimes_ms"].__setitem__(0, 0),
            lambda d: d["runs"][0].update(frame_digest="bad"),
            lambda d: d["runs"][0].update(frame_digest="ffffffffffffffff"),
            lambda d: d.update(runs=[d["runs"][0]]),
            lambda d: d.update(qemu={}),
            lambda d: d["runs"][0].update(warmup_frames=-1),
            lambda d: d["runs"][0].update(guest_peak_bytes=2**80),
        ]
        for i, mutate in enumerate(mutations):
            with self.subTest(case=i):
                sample=copy.deepcopy(fixture())
                mutate(sample)
                self.assertTrue(validator.validate(sample))
    def test_malformed_roots(self):
        for payload in (None, [], {}, {"runs":[]}, {"runs":[{},{}]}):
            self.assertTrue(validator.validate(payload))
if __name__=="__main__":
    unittest.main()
