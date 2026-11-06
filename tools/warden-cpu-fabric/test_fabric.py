#!/usr/bin/env python3
"""Unit and simulated-QEMU integration tests for the host-side Warden fabric."""
import json
from pathlib import Path
import tempfile
import threading
import unittest
import fabric


class InputPolicyTests(unittest.TestCase):
    def test_invalid_urls_are_denied(self):
        for url in ("http://localhost:9443", "https://u:p@host:9443",
                    "https://host:9443/shell", "https://host:9443/?a=1",
                    "https://host:9443#f", "https://host", "file:///tmp/a",
                    "https://host:99999"):
            with self.subTest(url=url), self.assertRaises((ValueError, fabric.PolicyError)):
                fabric.validate_url(url)

    def test_node_file_denies_duplicate_nodes(self):
        with tempfile.TemporaryDirectory() as tmp:
            nodefile = Path(tmp) / "nodes.json"
            nodefile.write_text('{"nodes":["https://localhost:9443","https://localhost:9444"]}')
            self.assertEqual(len(fabric.read_nodes(str(nodefile))), 2)
            nodefile.write_text('{"nodes":["https://localhost:9443","https://localhost:9443"]}')
            with self.assertRaises(fabric.PolicyError):
                fabric.read_nodes(str(nodefile))

    def test_sha_is_strict(self):
        for bad in ("A" * 64, "0" * 63, "", "../etc/passwd"):
            with self.assertRaises(fabric.PolicyError):
                fabric.validate_sha(bad)
        self.assertEqual(fabric.validate_sha("a" * 64), "a" * 64)

    def test_fixed_qemu_arguments(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "fake-qemu"
            exe.write_text("#!/bin/sh\nexit 1\n")
            exe.chmod(0o755)
            img = Path(tmp) / "image.mb32"
            img.write_bytes(b"Warden")
            sha = fabric.sha256_file(img)
            policy = fabric.Policy(str(exe), str(img), sha)
            cmd = policy.argv(Path(tmp) / "snapshot.mb32")
            self.assertEqual(cmd[cmd.index("-smp") + 1], "1")
            self.assertEqual(cmd[cmd.index("-m") + 1], "512M")
            self.assertEqual(cmd[cmd.index("-nic") + 1], "none")
            self.assertEqual(cmd[cmd.index("-kernel") + 1], str(Path(tmp) / "snapshot.mb32"))
            with self.assertRaises(fabric.PolicyError):
                fabric.Policy(str(exe), str(img), "0" * 64)

    def test_invalid_slots_rejected(self):
        with self.assertRaises(fabric.PolicyError):
            fabric.WorkPool(0, "a" * 64)
        with self.assertRaises(fabric.PolicyError):
            fabric.WorkPool(257, "a" * 64)


class WorkPoolTests(unittest.TestCase):
    def test_deduplicated_capacity_and_result(self):
        running = threading.Event()
        gate = threading.Event()
        count = []
        def task(_policy):
            count.append(1)
            running.set()
            gate.wait(timeout=5)
            return {"ok": True}

        pool = fabric.WorkPool(1, "a" * 64, runner=task)
        try:
            first, accepted = pool.submit("work-1", "a" * 64)
            self.assertTrue(accepted)
            self.assertTrue(running.wait(2))
            again, accepted = pool.submit("work-1", "a" * 64)
            self.assertFalse(accepted)
            self.assertIs(first, again)
            with self.assertRaises(fabric.BusyError):
                pool.submit("work-2", "a" * 64)
            with self.assertRaises(fabric.PolicyError):
                pool.submit("work-3", "b" * 64)
            with self.assertRaises(fabric.PolicyError):
                pool.submit("../bad", "a" * 64)
            self.assertEqual(pool.status("work-1")["state"], "running")
            gate.set()
            self.assertTrue(first.result(timeout=2)["ok"])
            self.assertEqual(pool.status("work-1")["state"], "finished")
            self.assertEqual(len(count), 1)
            second, _ = pool.submit("work-2", "a" * 64)
            self.assertTrue(second.result(timeout=2)["ok"])
        finally:
            gate.set()
            pool.close()

    def test_two_slots_are_concurrent(self):
        rendezvous = threading.Barrier(2)
        def task(_policy):
            rendezvous.wait(timeout=3)
            return {"ok": True}
        pool = fabric.WorkPool(2, "a" * 64, runner=task)
        try:
            a, _ = pool.submit("a", "a" * 64)
            b, _ = pool.submit("b", "a" * 64)
            self.assertTrue(a.result(timeout=3)["ok"])
            self.assertTrue(b.result(timeout=3)["ok"])
            self.assertEqual(pool.health()["active"], 0)
        finally:
            pool.close()

    def test_runner_error_does_not_leak(self):
        def task(_policy):
            raise RuntimeError("host-private-data")
        pool = fabric.WorkPool(1, "a" * 64, runner=task)
        try:
            future, _ = pool.submit("x", "a" * 64)
            self.assertEqual(future.result(timeout=2)["error"], "worker-execution-failed")
        finally:
            pool.close()


class QemuProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.exe = self.root / "fake-qemu"
        self.image = self.root / "image"
        self.image.write_bytes(b"fixed-warden-image")
        self.digest = fabric.sha256_file(self.image)

    def script(self, program):
        self.exe.write_text("#!/usr/bin/env python3\n" + program)
        self.exe.chmod(0o755)

    def policy(self, timeout=3, log_cap=1024):
        return fabric.Policy(str(self.exe), str(self.image), self.digest,
                             timeout=timeout, log_cap=log_cap)

    def test_verdict_is_not_just_exit_code(self):
        self.script("import sys\nprint('ALL HELD: 201 checks, 0 failed')\nsys.exit(1)\n")
        result = fabric.run_warden(self.policy())
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["checks"], 201)
        self.script("import sys\nprint('ALL HELD: 201 checks, 0 failed')\nsys.exit(0)\n")
        self.assertFalse(fabric.run_warden(self.policy())["ok"])
        self.script("import sys\nprint('KERNEL TEST FAILED: 201 checks, 1 failed')\nsys.exit(3)\n")
        self.assertFalse(fabric.run_warden(self.policy())["ok"])

    def test_image_is_staged_and_pinned(self):
        self.script("import os,sys\n"
                    "p = sys.argv[sys.argv.index('-kernel')+1]\n"
                    "assert os.path.basename(p) == 'warden-kernel.mb32'\n"
                    "print('ALL HELD: 201 checks, 0 failed')\nsys.exit(1)\n")
        self.assertTrue(fabric.run_warden(self.policy())["ok"])

    def test_timeout_and_output_limit(self):
        self.script("import time\ntime.sleep(5)\n")
        self.assertEqual(fabric.run_warden(self.policy(timeout=.1))["error"], "timeout")
        self.script("print('x'*5000)\n")
        self.assertEqual(fabric.run_warden(self.policy(log_cap=1024))["error"], "output-limit")


if __name__ == "__main__":
    unittest.main()
