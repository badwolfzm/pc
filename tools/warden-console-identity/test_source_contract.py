"""Source-level regression guard; does NOT replace native QEMU syscall tests."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "crates/warden-kernel/src/thread.rs"


def body_between(text, a, b):
    start = text.index(a)
    stop = text.index(b, start + len(a))
    return text[start:stop]


class ConsoleAuthorityContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = SOURCE.read_text()

    def test_ordinary_endpoints_have_no_console_authority(self):
        self.assertRegex(
            self.src,
            r"Endpoint \{[^\n]*capacity, console: false,",
        )
        root = body_between(self.src, "pub fn root_endpoint(", "pub fn mint_to(")
        self.assertIn("ep.console = true", root)
        self.assertIn("k.endpoints.insert(id.0, ep)", root)
        self.assertNotIn("ep.console = true", body_between(
            self.src, "pub fn create_endpoint(", "pub fn create_", 
        ) if "pub fn create_" in self.src[self.src.index("pub fn create_endpoint(") + 25:] else "")

    def test_write_checks_live_cap_and_console_identity_before_serial(self):
        arm = body_between(self.src, "SYS_CONSOLE_WRITE => {", "SYS_INVOKE =>")
        assert_index = arm.index("k.endpoints.get(&r.object.0)")
        write_index = arm.index("crate::serial::write_bytes")
        self.assertLess(assert_index, write_index)
        self.assertIn("Rights::WRITE", arm)
        self.assertIn("ep.console", arm)
        self.assertIn("ObjectKind::Endpoint", arm)
        self.assertIn("Err(err(CapError::WrongKind))", arm)
        self.assertIn("read_user(b,", arm)

    def test_log_checks_identity_before_judging(self):
        arm = body_between(self.src, "SYS_LOG => {", "SYS_CONSOLE_WRITE =>")
        self.assertIn("Rights::WRITE", arm)
        self.assertIn("ep.console", arm)
        self.assertLess(arm.index("ep.console"), arm.index("crate::tests::judge"))

    def test_retyped_endpoint_cannot_inherit_console_authority(self):
        retype = body_between(self.src, "fn retype(", "fn ipc(")
        self.assertIn("Endpoint::with_capacity(capacity)", retype)
        self.assertNotIn("ep.console = true", retype)


if __name__ == "__main__":
    unittest.main()
