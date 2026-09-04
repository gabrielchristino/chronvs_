Import("env")
from os.path import join
import os

# On this PC CC/CXX point to a desktop MinGW GCC. ESP-IDF must let PlatformIO
# select its bundled Xtensa cross-compiler instead.
os.environ.pop("CC", None)
os.environ.pop("CXX", None)
env["ENV"].pop("CC", None)
env["ENV"].pop("CXX", None)

# Compile the native ESP-IDF 5.3 driver supplied by Waveshare. It enables the
# SPD2010's four-wire QSPI transfer mode, unavailable in Arduino-ESP32 2.x.
driver_dir = join(
    env["PROJECT_DIR"],
    ".vendor-reference", "example", "ESP-IDF-5.3.2", "ESP32-S3-Touch-LCD-1.46-Test", "main"
)
lvgl_dir = join(
    env["PROJECT_DIR"], ".vendor-reference", "example", "ESP-IDF-5.3.2",
    "ESP32-S3-Touch-LCD-1.46-Test", "components", "lvgl__lvgl"
)

# The stock colour test uses 16 bands of 25 px (400 px total), leaving the
# final 12 rows of the 412 px panel unchanged. Keep the correction here rather
# than asking contributors to manually edit the cloned official reference.
display_source = join(driver_dir, "LCD_Driver", "Display_SPD2010.c")
with open(display_source, "r", encoding="utf-8") as source_file:
    display_text = source_file.read()

old_test = """  uint8_t byte_per_pixel = EXAMPLE_LCD_COLOR_BITS / 8;
  uint8_t *color = (uint8_t *)heap_caps_calloc(1, EXAMPLE_LCD_WIDTH * (EXAMPLE_LCD_HEIGHT / EXAMPLE_LCD_COLOR_BITS) * byte_per_pixel, MALLOC_CAP_DMA);


  for (int j = 0; j < EXAMPLE_LCD_COLOR_BITS; j++) {
      for (int i = 0; i < EXAMPLE_LCD_WIDTH * (EXAMPLE_LCD_HEIGHT / EXAMPLE_LCD_COLOR_BITS); i++) {
          for (int k = 0; k < byte_per_pixel; k++) {
              color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), EXAMPLE_LCD_COLOR_BITS) >> (k * 8)) & 0xff;
          }
      }
      esp_lcd_panel_draw_bitmap(panel_handle, 0, j * (EXAMPLE_LCD_HEIGHT / EXAMPLE_LCD_COLOR_BITS), EXAMPLE_LCD_WIDTH, (j + 1) * (EXAMPLE_LCD_HEIGHT / EXAMPLE_LCD_COLOR_BITS), color);
  }
  free(color);"""
new_test = """  uint8_t byte_per_pixel = EXAMPLE_LCD_COLOR_BITS / 8;
  const uint16_t max_band_height = (EXAMPLE_LCD_HEIGHT + EXAMPLE_LCD_COLOR_BITS - 1) / EXAMPLE_LCD_COLOR_BITS;
  uint8_t *color = (uint8_t *)heap_caps_calloc(1, EXAMPLE_LCD_WIDTH * max_band_height * byte_per_pixel, MALLOC_CAP_DMA);

  for (int j = 0; j < EXAMPLE_LCD_COLOR_BITS; j++) {
      const uint16_t y_start = (j * EXAMPLE_LCD_HEIGHT) / EXAMPLE_LCD_COLOR_BITS;
      const uint16_t y_end = ((j + 1) * EXAMPLE_LCD_HEIGHT) / EXAMPLE_LCD_COLOR_BITS;
      const uint16_t band_height = y_end - y_start;
      for (int i = 0; i < EXAMPLE_LCD_WIDTH * band_height; i++) {
          for (int k = 0; k < byte_per_pixel; k++) {
              color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), EXAMPLE_LCD_COLOR_BITS) >> (k * 8)) & 0xff;
          }
      }
      esp_lcd_panel_draw_bitmap(panel_handle, 0, y_start, EXAMPLE_LCD_WIDTH, y_end, color);
  }
  free(color);"""

if old_test in display_text:
    with open(display_source, "w", encoding="utf-8", newline="") as source_file:
        source_file.write(display_text.replace(old_test, new_test))

env.Append(CPPPATH=[
    join(driver_dir, "I2C_Driver"),
    join(driver_dir, "EXIO"),
    join(driver_dir, "Touch_Driver"),
    join(driver_dir, "LCD_Driver"),
    join(driver_dir, "LCD_Driver", "esp_lcd_spd2010"),
    join(driver_dir, "LVGL_Driver"),
    lvgl_dir,
    join(lvgl_dir, "src"),
    join(env["PROJECT_DIR"], "src"),
])

env.BuildSources(
    join("$BUILD_DIR", "waveshare_idf_drivers"),
    driver_dir,
    src_filter=" ".join([
        "+<I2C_Driver/I2C_Driver.c>",
        "+<EXIO/TCA9554PWR.c>",
        "+<Touch_Driver/Touch_SPD2010.c>",
        "+<LCD_Driver/Display_SPD2010.c>",
        "+<LCD_Driver/esp_lcd_spd2010/esp_lcd_spd2010.c>",
        "+<LVGL_Driver/LVGL_Driver.c>",
    ]),
)

# Use the exact LVGL 8.3.11 component distributed in the Waveshare ESP-IDF
# example. It provides correct text rasterisation and the vendor's flush/touch
# callbacks for this QSPI panel.
env.BuildSources(
    join("$BUILD_DIR", "waveshare_lvgl"),
    lvgl_dir,
    src_filter="+<src/>",
)
