"""Verify capture keeps stdout without enabling interfering task logs."""
from pathlib import Path
import runpy
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LoggingTest(unittest.TestCase):
    def test_console_and_log_policies(self):
        for policy, log_level, console in (("no", 0, "NONE"), ("yes", 3, "UART_DEFAULT"),
                                            ("capture", 0, "UART_DEFAULT")):
            with self.subTest(policy=policy), tempfile.TemporaryDirectory() as temporary:
                config = Path(temporary) / "sdkconfig.test"
                config.write_text("CONFIG_SPIRAM=y\nCONFIG_LOG_DEFAULT_LEVEL_INFO=y\n", encoding="utf-8")
                class Environment(dict):
                    def GetProjectOption(self, name, default):
                        return {"custom_serial_diagnostics": policy}.get(name, default)

                    def subst(self, value):
                        return "test"
                env = Environment(PROJECT_DIR=temporary)
                runpy.run_path(str(ROOT / "scripts/configure_logging.py"),
                               init_globals={"env": env, "Import": lambda _: None})
                text = config.read_text(encoding="utf-8")
                self.assertIn(f"CONFIG_LOG_DEFAULT_LEVEL={log_level}\n", text)
                self.assertIn(f"CONFIG_LOG_MAXIMUM_LEVEL={log_level}\n", text)
                self.assertIn(f"CONFIG_ESP_CONSOLE_{console}=y\n", text)
                self.assertIn("CONFIG_SPIRAM=y\n", text)


if __name__ == "__main__":
    unittest.main()
