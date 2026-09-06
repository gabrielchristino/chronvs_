"""Exercise the persistent driver patch and rollback on a temporary vendor copy."""
import contextlib
import io
import os
from pathlib import Path
import runpy
import shutil
import tempfile
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
DRIVER = Path('.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/main')
FILES = [
    'LCD_Driver/Display_SPD2010.c', 'LCD_Driver/Display_SPD2010.h',
    'LCD_Driver/esp_lcd_spd2010/esp_lcd_spd2010.c',
    'LVGL_Driver/LVGL_Driver.c', 'LVGL_Driver/LVGL_Driver.h',
]


class BuildEnvironment(dict):
    def Append(self, **kwargs):
        pass

    def BuildSources(self, *args, **kwargs):
        pass


def main():
    with tempfile.TemporaryDirectory(prefix='chronvs-driver-') as directory:
        root = Path(directory)
        for name in FILES:
            target = root / DRIVER / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / DRIVER / name, target)

        def apply(enabled):
            env = BuildEnvironment(PROJECT_DIR=str(root), ENV={})
            with patch.dict(os.environ, CHRONVS_QSPI_DRAIN=str(int(enabled))):
                with contextlib.redirect_stdout(io.StringIO()):
                    runpy.run_path(str(ROOT / 'scripts/add_waveshare_drivers.py'),
                                   init_globals={'Import': lambda name: None, 'env': env})
            return {name: (root / DRIVER / name).read_text(encoding='utf-8') for name in FILES}

        enabled = apply(True)
        assert enabled == apply(True), 'Enabled patch is not idempotent'
        disabled = apply(False)
        assert disabled == apply(False), 'Rollback is not idempotent'
        assert apply(True) == enabled, 'Re-enabling did not restore the patched driver'
        fence = 'esp_lcd_panel_io_tx_param(io, -1, NULL, 0)'
        panel_name = FILES[2]
        assert enabled[panel_name].count(fence) == 1
        assert fence not in disabled[panel_name]
        for name in FILES:
            if name != panel_name:
                assert enabled[name] == disabled[name], f'Rollback changed {name}'
        assert '#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)' in enabled[FILES[4]]
        assert '#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE   (2048)' in enabled[FILES[1]]
        assert 'lv_disp_flush_ready(drv);' in enabled[FILES[3]]
    print('Waveshare patch: idempotence, rollback and display limits passed.')


if __name__ == '__main__':
    main()
