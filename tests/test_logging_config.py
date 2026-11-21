import pathlib
import tempfile
import unittest

SCRIPT = (pathlib.Path(__file__).resolve().parents[1] / "scripts/configure_logging.py").read_text()


class LoggingConfigTests(unittest.TestCase):
    def configure(self, mode, cache_line="reference", instruction_cache="reference", bluetooth="no"):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            (root / "sdkconfig.test").write_text(
                "CONFIG_ESP_CONSOLE_UART_DEFAULT=y\n"
                "CONFIG_ESP32S3_DATA_CACHE_32KB=y\n"
                "CONFIG_ESP32S3_DATA_CACHE_LINE_32B=y\n"
                "CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=32\n"
                "CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB=y\n"
                "CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE=0x4000\n"
                "CONFIG_SPIRAM_SPEED_80M=y\n"
                "# CONFIG_BT_ENABLED is not set\n")

            class Env(dict):
                def GetProjectOption(self, name, default=""):
                    return {"custom_serial_diagnostics": mode, "build_type": "release",
                            "custom_data_cache_line": cache_line,
                            "custom_instruction_cache": instruction_cache,
                            "custom_bluetooth_diagnostic": bluetooth}.get(name, default)

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

    def test_cache_line_experiment_preserves_capacity_and_clock(self):
        reference = self.configure("usb")
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_LINE_32B=y\n", reference)
        self.assertNotIn("CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y\n", reference)
        candidate = self.configure("usb", "64")
        self.assertIn("# CONFIG_ESP32S3_DATA_CACHE_LINE_32B is not set\n", candidate)
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y\n", candidate)
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=64\n", candidate)
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_32KB=y\n", candidate)
        self.assertIn("CONFIG_SPIRAM_SPEED_80M=y\n", candidate)

    def test_invalid_cache_line_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "custom_data_cache_line"):
            self.configure("usb", "128")

    def test_instruction_experiment_keeps_data_cache_and_clock(self):
        reference = self.configure("usb")
        self.assertIn("CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB=y\n", reference)
        candidate = self.configure("usb", instruction_cache="32")
        self.assertIn("# CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB is not set\n", candidate)
        self.assertIn("CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y\n", candidate)
        self.assertIn("CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE=0x8000\n", candidate)
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_32KB=y\n", candidate)
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_LINE_32B=y\n", candidate)
        self.assertIn("CONFIG_SPIRAM_SPEED_80M=y\n", candidate)

    def test_invalid_instruction_cache_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "custom_instruction_cache"):
            self.configure("usb", instruction_cache="64")

    def test_ble_is_opt_in_and_preserves_usb_and_hardware_policy(self):
        normal = self.configure("no")
        self.assertIn("# CONFIG_BT_ENABLED is not set\n", normal)
        self.assertNotIn("CONFIG_BT_BLUEDROID_ENABLED=y\n", normal)
        bench = self.configure("usb", bluetooth="yes")
        self.assertIn("CONFIG_BT_ENABLED=y\n", bench)
        self.assertIn("CONFIG_BT_BLUEDROID_ENABLED=y\n", bench)
        self.assertIn("CONFIG_BT_BLE_ENABLED=y\n", bench)
        self.assertIn("CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y\n", bench)
        self.assertIn("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\n", bench)
        self.assertIn("CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=32\n", bench)
        self.assertIn("CONFIG_SPIRAM_SPEED_80M=y\n", bench)


if __name__ == "__main__":
    unittest.main()
