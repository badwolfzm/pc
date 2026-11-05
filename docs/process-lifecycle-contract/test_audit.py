import json
import tempfile
import unittest
from pathlib import Path
from audit import audit, extract_pass_arms, main

SOURCE = '''fn pass(k: &mut Kernel, cur: usize, nr: u32, name: &str, a: &[u64; 6]) -> Result<After, Death> {
    match name {
        "rt_sigaction" | "rt_sigprocmask" | "set_robust_list" => ret(Ok(0)),
        "getpid" => ret(Ok(1000 + cur as u64)),
        _ => not_built(k, cur, "pass", name),
    }
}
/// Mediated tier: external policy
'''
ABI = {"syscalls": [{"name": "rt_sigaction", "nr": 13, "class": "pass"},
                    {"name": "rt_sigprocmask", "nr": 14, "class": "pass"},
                    {"name": "set_robust_list", "nr": 273, "class": "pass"},
                    {"name": "clone", "nr": 56, "class": "mediated"},
                    {"name": "futex", "nr": 202, "class": "pass"}]}


class AuditTests(unittest.TestCase):
    def test_detects_silent_success(self):
        result = audit(ABI, SOURCE)
        self.assertEqual(result["summary"]["silent_success"], 3)
        self.assertEqual({r["name"] for r in result["rows"] if r["source_status"] == "silent_success"},
                         {"rt_sigaction", "rt_sigprocmask", "set_robust_list"})

    def test_missing_dispatch_arm_not_claimed_implemented(self):
        rows = {r["name"]: r for r in audit(ABI, SOURCE)["rows"]}
        self.assertEqual(rows["futex"]["source_status"], "no_explicit_pass_arm")
        self.assertEqual(rows["clone"]["source_status"], "outside_pass_dispatch")

    def test_mutation_detects_stub_replacement(self):
        original = audit(ABI, SOURCE)["summary"]["silent_success"]
        changed = SOURCE.replace("=> ret(Ok(0)),", "=> ret(Err(ENOSYS)),")
        self.assertEqual(original - audit(ABI, changed)["summary"]["silent_success"], 3)

    def test_rejects_missing_dispatch(self):
        with self.assertRaisesRegex(ValueError, "dispatcher not found"):
            extract_pass_arms("not rust")

    def test_rejects_duplicate_abi(self):
        with self.assertRaisesRegex(ValueError, "duplicate ABI"):
            audit({"syscalls": ABI["syscalls"] * 2}, SOURCE)

    def test_assertion_exit_codes(self):
        with tempfile.TemporaryDirectory() as d:
            abi = Path(d) / "abi.json"
            linux = Path(d) / "linux.rs"
            abi.write_text(json.dumps(ABI))
            linux.write_text(SOURCE)
            from contextlib import redirect_stdout
            import io
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main(["--abi", str(abi), "--linux", str(linux)]), 0)
                self.assertEqual(main(["--abi", str(abi), "--linux", str(linux), "--assert-no-silent-success"]), 1)
                linux.write_text(SOURCE.replace("=> ret(Ok(0)),", "=> ret(Err(ENOSYS)),"))
                self.assertEqual(main(["--abi", str(abi), "--linux", str(linux), "--assert-no-silent-success"]), 0)


if __name__ == "__main__":
    unittest.main()
