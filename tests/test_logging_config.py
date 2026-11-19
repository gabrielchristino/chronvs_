import pathlib
import tempfile
import unittest

SCRIPT = (pathlib.Path(__file__).resolve().parents[1] / "scripts/configure_logging.py").read_text()


class LoggingConfigTests(unittest.TestCase):
    def configure(self, mode):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            (root / "sdkconfig.test").write_text("CONFIG_ESP_CONSOLE_UART_DEFAULT=y\n")

            class Env(dict):
                def GetProjectOption(self, name, default=""):
                    return {"custom_serial_diagnostics": mode, "build_type": "release"}.get(name, default)

                def subst(self, value):
                    return value.replace("$PIOENV", "test")

            namespace = {"env": Env(PROJECT_DIR=str(root)), "Import": lambda _: None}
            exec(compile(SCRIPT, "configure_logging.py", "exec"), namespace)
            return (root / "sdkconfig.test").read_text()

    def test_usb_owns_no_gnss_tx_pin(self):
        config = self.configure("usb")
        self.assertIn("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\n", config)
        self.assertIn("# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set\n", config)
        self.assertIn("CONFIG_ESP_CONSOLE_UART_NONE=y\n", config)
        self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_NONE=y\n", config)
        self.assertIn("CONFIG_LOG_DEFAULT_LEVEL=3\n", config)

    def test_existing_modes_preserve_console_policy(self):
        normal = self.configure("no")
        self.assertIn("CONFIG_ESP_CONSOLE_NONE=y\n", normal)
        self.assertIn("CONFIG_LOG_DEFAULT_LEVEL=0\n", normal)
        for mode in ("yes", "capture"):
            config = self.configure(mode)
            self.assertIn("CONFIG_ESP_CONSOLE_UART_DEFAULT=y\n", config)
            self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y\n", config)


if __name__ == "__main__":
    unittest.main()
