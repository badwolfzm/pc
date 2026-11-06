import unittest
from validate import validate

BASE = ["qemu-system-x86_64", "-accel", "tcg",
        "-machine", "pc", "-smp", "1", "-m", "512M",
        "-display", "none", "-serial", "stdio", "-no-reboot",
        "-kernel", "/tmp/warden-kernel.mb32",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"]


class QemuArgvTests(unittest.TestCase):
    def test_valid_tcg(self):
        self.assertEqual(validate(BASE)["guest_ram_mib_requested"], 512)

    def test_valid_windows_whpx_path(self):
        a = BASE.copy()
        a[0] = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
        a[2] = "whpx"
        a[15] = r"C:\Users\Example\warden-kernel.mb32"
        self.assertEqual(validate(a)["acceleration_requested"], "whpx")

    def test_kvm(self):
        a = BASE.copy()
        a[2] = "kvm"
        self.assertEqual(validate(a)["vcpus_requested"], 1)

    def reject(self, a):
        with self.assertRaises(ValueError):
            validate(a)

    def test_reject_two_cpus(self):
        a = BASE.copy()
        a[6] = "2"
        self.reject(a)

    def test_reject_1024m(self):
        a = BASE.copy()
        a[8] = "1024M"
        self.reject(a)

    def test_reject_duplicate_memory_flag(self):
        self.reject(BASE + ["-m", "1024M"])

    def test_reject_extra_host_fs_device(self):
        self.reject(BASE + ["-drive", "file=/etc/shadow"])

    def test_reject_unapproved_accel(self):
        a = BASE.copy()
        a[2] = "foo"
        self.reject(a)

    def test_reject_bad_machine(self):
        a = BASE.copy()
        a[4] = "q35"
        self.reject(a)

    def test_reject_extra_device(self):
        a = BASE.copy()
        a[17] = "virtio-net"
        self.reject(a)

    def test_reject_shell_string(self):
        self.reject("qemu-system-x86_64 -smp 1")

    def test_reject_wrong_executable(self):
        a = BASE.copy()
        a[0] = "/tmp/evil"
        self.reject(a)

    def test_reject_option_in_kernel(self):
        a = BASE.copy()
        a[15] = "-monitor"
        self.reject(a)

    def test_reject_newline_kernel(self):
        a = BASE.copy()
        a[15] = "/tmp/kernel\n-malicious"
        self.reject(a)

    def test_reject_missing_kernel(self):
        a = BASE.copy()
        a[15] = ""
        self.reject(a)

    def test_reject_booleans(self):
        a = BASE.copy()
        a[6] = True
        self.reject(a)


if __name__ == "__main__":
    unittest.main()
