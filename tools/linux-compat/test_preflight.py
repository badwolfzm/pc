#!/usr/bin/env python3
"""Deterministic guard regressions for the read-only loader preflight."""
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("preflight", Path(__file__).with_name("preflight.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def image(etype=2, machine=62, extra=(), loads=None):
    if loads is None:
        loads = [(1, 5, 0x400000, b"\x90" * 16, 4096)]
    segments = list(loads) + list(extra)
    hdr = bytearray(64 + 56 * len(segments))
    hdr[:4] = b"\x7fELF"
    hdr[4:7] = b"\x02\x01\x01"
    struct.pack_into("<HHI", hdr, 16, etype, machine, 1)
    struct.pack_into("<QQ", hdr, 24, loads[0][2] if loads else 0x400000, 64)
    struct.pack_into("<HHH", hdr, 52, 64, 56, len(segments))
    for i, (ptype, flags, va, payload, memsz) in enumerate(segments):
        off = 64 + 56 * i
        offset = len(hdr)
        hdr.extend(payload)
        struct.pack_into("<IIQQQQQQ", hdr, off, ptype, flags, offset, va, 0, len(payload), memsz, 4096)
    return bytes(hdr)


class PreflightTests(unittest.TestCase):
    def check(self, blob, reason):
        result = mod.inspect_bytes(blob)
        self.assertIn(reason, result["reasons"])
        self.assertFalse(result["candidate_for_current_loader"])

    def test_static_candidate(self):
        result = mod.inspect_bytes(image())
        self.assertTrue(result["candidate_for_current_loader"])
        self.assertEqual(result["classification"], "static_elf_loader_candidate")

    def test_bad_magic(self):
        self.check(b"NO" + image()[2:], "bad_magic")

    def test_truncated_header(self):
        self.check(b"\x7fELF", "truncated_elf_header")

    def test_wrong_arch(self):
        self.check(image(machine=183), "requires_x86_64")

    def test_pie(self):
        self.check(image(etype=3), "requires_static_et_exec_not_pie")

    def test_interpreter(self):
        self.check(image(extra=[(3, 4, 0, b"/lib64/ld-linux-x86-64.so.2\0", 32)]),
                   "pt_interp_dynamic_loader_unavailable")

    def test_dynamic(self):
        self.check(image(extra=[(2, 4, 0, b"\0" * 16, 16)]),
                   "pt_dynamic_not_supported_as_general_linux_linking")

    def test_rwx(self):
        self.check(image(loads=[(1, 7, 0x400000, b"\x90", 4096)]),
                   "writable_executable_segment")

    def test_out_of_range(self):
        self.check(image(loads=[(1, 5, 0x300000, b"\x90", 4096)]),
                   "segment_outside_warden_window")

    def test_overlap(self):
        self.check(image(loads=[(1, 5, 0x400000, b"\x90", 1024),
                                (1, 4, 0x400800, b"\x90", 2048)]),
                   "page_granular_segment_overlap")

    def test_more_than_eight(self):
        loads = [(1, 5 if i == 0 else 4, 0x400000 + i * 4096, b"\x90", 4096)
                 for i in range(9)]
        self.check(image(loads=loads), "too_many_load_segments")

    def test_invalid_file_bounds(self):
        data = bytearray(image())
        struct.pack_into("<Q", data, 64 + 8, len(data) + 400)
        self.check(bytes(data), "segment_file_bounds_invalid")

    def test_bad_program_header_size(self):
        data = bytearray(image())
        struct.pack_into("<H", data, 54, 40)
        self.check(bytes(data), "invalid_program_header_entry_size")

    def test_bad_program_table(self):
        data = bytearray(image())
        struct.pack_into("<Q", data, 32, len(data) + 1)
        self.check(bytes(data), "program_header_table_out_of_bounds")

    def test_no_executable_entry(self):
        self.check(image(loads=[(1, 4, 0x400000, b"\x90", 4096)]),
                   "entry_not_in_executable_segment")

    def test_native_window(self):
        self.assertFalse(mod.inspect_bytes(image(), "native")["candidate_for_current_loader"])
        self.assertTrue(mod.inspect_bytes(
            image(loads=[(1, 5, 0x800000, b"\x90", 4096)]), "native")["candidate_for_current_loader"])

    def test_real_host_elf_is_not_mistaken_for_warden_execution(self):
        # A host's ELF may classify as blocked (usually PIE/dynamic) or candidate.
        # Either result is preflight-only and never implies the binary booted.
        for path in (Path("/bin/true"), Path("/usr/bin/env")):
            if path.is_file():
                result = mod.inspect_bytes(path.read_bytes())
                self.assertIn("Preflight only", result["note"])
                self.assertIn(result["classification"],
                              ("blocked", "static_elf_loader_candidate"))


if __name__ == "__main__":
    unittest.main()
