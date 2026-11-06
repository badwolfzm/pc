"""Unit tests for the host-side QEMU argv preflight."""
import unittest
from guard import PolicyError, validate

BASE = ["qemu-system-x86_64", "-smp", "1", "-m", "512M",
        "-kernel", "/operator/pinned/warden-kernel.mb32",
        "-display", "none", "-serial", "stdio"]


class GuardTests(unittest.TestCase):
    def test_approved_exact_command(self):
        validate(BASE[:], BASE[:])

    def test_remote_argument_injection(self):
        with self.assertRaises(PolicyError):
            validate(BASE + ["-monitor", "tcp:0.0.0.0:4444,server,nowait"], BASE)

    def test_changed_kernel_path(self):
        other = BASE[:]
        other[other.index("-kernel") + 1] = "/tmp/attacker-kernel"
        with self.assertRaises(PolicyError):
            validate(other, BASE)

    def test_wrong_cpu(self):
        other = BASE[:]
        other[other.index("-smp") + 1] = "2"
        with self.assertRaises(PolicyError):
            validate(other, other)

    def test_wrong_ram(self):
        other = BASE[:]
        other[other.index("-m") + 1] = "1024"
        with self.assertRaises(PolicyError):
            validate(other, other)

    def test_duplicate_memory(self):
        other = BASE + ["-m", "512M"]
        with self.assertRaises(PolicyError):
            validate(other, other)

    def test_disallowed_host_monitor_even_if_approved(self):
        other = BASE + ["-monitor", "stdio"]
        with self.assertRaises(PolicyError):
            validate(other, other)

    def test_spoofed_executable_suffix(self):
        other = BASE[:]
        other[0] = "/tmp/evil-qemu-system-x86_64"
        with self.assertRaises(PolicyError):
            validate(other, other)

    def test_ambiguous_memory_units(self):
        other = BASE[:]
        other[other.index("-m") + 1] = "512"
        with self.assertRaises(PolicyError):
            validate(other, other)

    def test_malformed_input_types(self):
        for candidate, approved in ((None, BASE), (BASE, None), ("bad", BASE),
                                    (BASE, "bad"), (BASE + [None], BASE + [None])):
            with self.subTest(candidate=candidate, approved=approved):
                with self.assertRaises(PolicyError):
                    validate(candidate, approved)

    def test_invalid_executable(self):
        other = BASE[:]
        other[0] = "/bin/sh"
        with self.assertRaises(PolicyError):
            validate(other, other)


if __name__ == "__main__":
    unittest.main()
