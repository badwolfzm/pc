"""Unit checks for bounded swarm evidence manifests."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from verify import verify

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.manifest = self.root / "manifest.json"
        self.runs = []
        for n in (1,2):
            name = f"node{n}.log"
            blob = f"guest{n} done".encode()
            (self.root / name).write_bytes(blob)
            self.runs.append(dict(node_id=f"node{n}",job_id=f"job{n}",vcpu=1,ram_mib=512,
                kernel_sha256="a"*64,serial_path=name,serial_sha256=hashlib.sha256(blob).hexdigest(),
                guest_exit_code=0,elapsed_ms=10))
    def check(self):
        self.manifest.write_text(json.dumps({"runs":self.runs}))
        return verify(self.manifest)
    def test_valid(self):
        self.assertEqual(self.check(),[])
    def test_two_vcpu_rejected(self):
        self.runs[0]["vcpu"]=2
        self.assertTrue(self.check())
    def test_ram_rejected(self):
        self.runs[0]["ram_mib"]=513
        self.assertTrue(self.check())
    def test_duplicate_nodes_rejected(self):
        self.runs[1]["node_id"]="node1"
        self.assertTrue(self.check())
    def test_duplicate_jobs_rejected(self):
        self.runs[1]["job_id"]="job1"
        self.assertTrue(self.check())
    def test_duplicate_evidence_rejected(self):
        self.runs[1]["serial_path"]="node1.log"
        self.assertTrue(self.check())
    def test_tampering_rejected(self):
        (self.root/"node1.log").write_bytes(b"changed")
        self.assertTrue(self.check())
    def test_traversal_rejected(self):
        self.runs[0]["serial_path"]="../outside"
        self.assertTrue(self.check())
    def test_symlink_parent_rejected(self):
        (self.root/"alias").symlink_to(self.root,target_is_directory=True)
        self.runs[0]["serial_path"]="alias/node1.log"
        self.assertTrue(self.check())
    def test_failed_exit_rejected(self):
        self.runs[0]["guest_exit_code"]=1
        self.assertTrue(self.check())
    def test_boolean_bypass_rejected(self):
        self.runs[0]["vcpu"]=True
        self.assertTrue(self.check())
    def test_schema_rejected(self):
        self.runs[0]["extra"]="bad"
        self.assertTrue(self.check())

if __name__ == "__main__":
    unittest.main()
