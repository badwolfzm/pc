import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).with_name("measure.py")
spec = importlib.util.spec_from_file_location("doom_measure", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RealGameMeasurementContract(unittest.TestCase):
    def test_pinned_sha_and_freedoom_digest_are_exact(self):
        self.assertEqual(len(module.PIN), 40)
        self.assertEqual(len(module.FREEDOOM1_SHA256), 64)
        self.assertEqual(module.FREEDOOM1_SHA256, "7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d")

    def test_refuses_unapproved_game_asset(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source"
            source.mkdir()
            wad = Path(tmp) / "game.wad"
            wad.write_bytes(b"IWAD" + b"0" * 24)
            with patch.object(module.subprocess, "check_output", return_value=module.PIN):
                with self.assertRaisesRegex(ValueError, "SHA mismatch"):
                    module.verify_source_and_asset(source, wad)

    def test_refuses_wrong_upstream_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source"
            source.mkdir()
            wad = Path(tmp) / "game.wad"
            wad.write_bytes(b"IWAD" + b"0" * 24)
            with patch.object(module.subprocess, "check_output", return_value="1" * 40):
                with self.assertRaisesRegex(ValueError, "unapproved upstream"):
                    module.verify_source_and_asset(source, wad)

    def test_refuses_symlink_asset(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source"
            source.mkdir()
            real = Path(tmp) / "real.wad"
            real.write_bytes(b"IWAD" + b"0" * 24)
            alias = Path(tmp) / "alias.wad"
            alias.symlink_to(real)
            with self.assertRaisesRegex(ValueError, "regular game asset"):
                module.verify_source_and_asset(source, alias)

if __name__ == "__main__":
    unittest.main()
