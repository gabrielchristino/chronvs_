"""Exercise logging policy without PlatformIO or changing real sdkconfig."""
from pathlib import Path
import runpy
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/configure_logging.py"


class Environment(dict):
    def GetProjectOption(self, key, default):
        return self.get(key, default)

    def subst(self, value):
        return "test"


class LoggingPolicyTest(unittest.TestCase):
    def test_diagnostic_inherits_reference_without_modifying_it(self):
        with tempfile.TemporaryDirectory() as directory:
            reference = Path(directory) / "sdkconfig.reference"
            content = "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192\nCONFIG_LOG_DEFAULT_LEVEL_NONE=y\n"
            reference.write_text(content, encoding="utf-8")
            env = Environment(PROJECT_DIR=directory, custom_serial_diagnostics="yes",
                              custom_sdkconfig_reference=reference.name)
            runpy.run_path(str(SCRIPT), init_globals={"env": env, "Import": lambda _: None})
            result = (Path(directory) / "sdkconfig.test").read_text(encoding="utf-8")
            self.assertEqual(reference.read_text(encoding="utf-8"), content)
            self.assertIn("CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192\n", result)
            self.assertIn("CONFIG_LOG_DEFAULT_LEVEL_INFO=y\n", result)
            self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y\n", result)

    def test_round_trip_preserves_hardware_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "sdkconfig.test"
            hardware = "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192\nCONFIG_SPIRAM_USE_MALLOC=y\n"
            config.write_text(hardware + "CONFIG_LOG_DEFAULT_LEVEL_INFO=y\n"
                              "CONFIG_ESP_CONSOLE_UART_DEFAULT=y\n", encoding="utf-8")
            env = Environment(PROJECT_DIR=directory)
            for mode, level, console in (("no", "0", "NONE"), ("yes", "3", "UART_DEFAULT"),
                                         ("no", "0", "NONE")):
                env["custom_serial_diagnostics"] = mode
                runpy.run_path(str(SCRIPT), init_globals={"env": env, "Import": lambda _: None})
                content = config.read_text(encoding="utf-8")
                self.assertTrue(content.startswith(hardware))
                self.assertIn("CONFIG_LOG_DEFAULT_LEVEL=" + level + "\n", content)
                self.assertIn("CONFIG_LOG_MAXIMUM_LEVEL=" + level + "\n", content)
                self.assertIn("CONFIG_BOOTLOADER_LOG_LEVEL=" + level + "\n", content)
                self.assertIn("CONFIG_ESP_CONSOLE_" + console + "=y\n", content)
                stamp = config.stat().st_mtime_ns
                runpy.run_path(str(SCRIPT), init_globals={"env": env, "Import": lambda _: None})
                self.assertEqual(content, config.read_text(encoding="utf-8"))
                self.assertEqual(stamp, config.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
