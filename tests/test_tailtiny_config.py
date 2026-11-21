import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPTS = [(ROOT / "scripts" / name).read_text(encoding="utf-8")
           for name in ("configure_tailtiny.py", "configure_logging.py")]


class TailTinyConfigTests(unittest.TestCase):
    def test_profile_is_repeatable_and_preserves_reference(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            reference = root / "sdkconfig.waveshare_esp32_s3_touch_lcd_146"
            original = ("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768\n"
                        "CONFIG_SPIRAM_MODE_OCT=y\n"
                        "CONFIG_LV_MEM_SIZE=131072\n"
                        "CONFIG_LWIP_MAX_SOCKETS=10\n"
                        "# CONFIG_MBEDTLS_CHACHAPOLY_C is not set\n")
            reference.write_text(original, encoding="utf-8")

            class Env(dict):
                def subst(self, value):
                    return value.replace("$PIOENV", "tailtiny")

                def GetProjectOption(self, name, default=""):
                    return {"custom_serial_diagnostics": "usb"}.get(name, default)

            env = Env(PROJECT_DIR=str(root))
            target = root / "sdkconfig.tailtiny"
            for script in SCRIPTS:
                exec(compile(script, "config-script", "exec"),
                     {"env": env, "Import": lambda _: None})
            first = target.read_bytes()
            for script in SCRIPTS:
                exec(compile(script, "config-script", "exec"),
                     {"env": env, "Import": lambda _: None})
            self.assertEqual(first, target.read_bytes())
            self.assertEqual(original, reference.read_text(encoding="utf-8"))
            text = target.read_text(encoding="utf-8")
            for required in ("CONFIG_MBEDTLS_CHACHAPOLY_C=y\n",
                             "CONFIG_ML_DERP_VERIFY_CERT=y\n",
                             "# CONFIG_ML_ENABLE_CONFIG_HTTPD is not set\n",
                             "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\n",
                             "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768\n",
                             "CONFIG_SPIRAM_MODE_OCT=y\n",
                             "CONFIG_LV_MEM_SIZE=131072\n"):
                self.assertIn(required, text)


if __name__ == "__main__":
    unittest.main()
