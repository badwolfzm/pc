import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).with_name("gate.py")
spec = importlib.util.spec_from_file_location("zone_gate", path)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

SOURCE = """
#define DEFAULT_RAM 6 /* MiB */
#define MIN_RAM 6 /* MiB */
static void *AutoAllocMemory(int *size, int default_ram, int min_ram) {
 *size = default_ram * 1024 * 1024;
 zonemem = malloc(*size);
 return zonemem;
}
byte *I_ZoneBase(int *size) {
 default_ram = DEFAULT_RAM;
 min_ram = MIN_RAM;
 zonemem = AutoAllocMemory(size, default_ram, min_ram);
 return zonemem;
}
"""

class ZoneTests(unittest.TestCase):
    def test_default_zone_exceeds_current_one_mib_arena(self):
        with self.assertRaisesRegex(gate.GateFailure, "cannot hold"):
            gate.verify(SOURCE, 1048576)

    def test_larger_candidate_is_only_capacity_preflight(self):
        result = gate.verify(SOURCE, 8 * gate.MIB)
        self.assertEqual(result["default_zone_mib"], 6)
        self.assertTrue(result["capacity_pass_for_default_zone"])
        self.assertFalse(result["candidate_doom_boot_verified"])

    def test_ambiguous_sources_and_truncated_shape_fail(self):
        for text in [SOURCE.replace("#define MIN_RAM 6", ""),
                     SOURCE.replace("malloc(*size)", "my_allocator(*size)"),
                     SOURCE.replace("default_ram = DEFAULT_RAM;", ""),
                     SOURCE.replace("#define MIN_RAM 6", "#define MIN_RAM 9"),
                     SOURCE + "\n#define MIN_RAM 6"]:
            with self.subTest(text=text[:100]), self.assertRaises(gate.GateFailure):
                gate.verify(text, 16 * gate.MIB)

    def test_overhead_not_equal_zone(self):
        with self.assertRaises(gate.GateFailure):
            gate.verify(SOURCE, 6 * gate.MIB)
        with self.assertRaises(gate.GateFailure):
            gate.verify(SOURCE, -1)

if __name__ == "__main__":
    unittest.main()
