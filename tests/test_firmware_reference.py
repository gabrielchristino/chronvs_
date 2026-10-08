import os
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
MODULE = runpy.run_path(str(ROOT / "scripts/firmware_reference.py"))


class ReferenceTests(unittest.TestCase):
    def test_preservation_integrity_and_custom_uploader(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / ".pio/build/test"
            for name in MODULE["FILES"]:
                path = build / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            (root / "scripts").mkdir()
            for name in ("sdkconfig.test", "platformio.ini", "scripts/upload_waveshare.py"):
                (root / name).write_text("test", encoding="utf-8")
            with patch("subprocess.check_output", return_value="test\n"):
                reference = MODULE["preserve"](root, "test")
            self.assertEqual(MODULE["verify"](reference), reference)
            # Changing the active build must not affect the saved reference.
            (build / "firmware.bin").write_bytes(b"new build")
            self.assertEqual((reference / "firmware.bin").read_bytes(), b"firmware.bin")

            class Environment:
                def subst(self, key):
                    return {"$BUILD_DIR": str(build), "$PROJECT_DIR": str(ROOT)}[key]

                def PioPlatform(self):
                    return self

                def get_package_dir(self, name):
                    return "esptool-package"

                def Replace(self, **values):
                    self.values = values

            env = Environment()
            with patch.dict(os.environ, {"CHRONVS_FIRMWARE_REFERENCE": str(reference)}):
                runpy.run_path(str(ROOT / "scripts/upload_waveshare.py"),
                               init_globals={"Import": lambda name: None, "env": env})
            flags = env.values["UPLOADERFLAGS"]
            for name, address in MODULE["IMAGES"].items():
                self.assertEqual(Path(flags[flags.index(address) + 1]), reference / name)
            (reference / "firmware.bin").write_bytes(b"corrupt")
            with self.assertRaises(ValueError):
                MODULE["verify"](reference)
            with patch.dict(os.environ, {"CHRONVS_FIRMWARE_REFERENCE": str(reference)}):
                with self.assertRaises(ValueError):
                    runpy.run_path(str(ROOT / "scripts/upload_waveshare.py"),
                                   init_globals={"Import": lambda name: None, "env": env})

    def test_missing_artifact_and_environment_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                MODULE["preserve"](directory, "test")
            with self.assertRaises(ValueError):
                MODULE["preserve"](directory, "../test")


if __name__ == "__main__":
    unittest.main()
