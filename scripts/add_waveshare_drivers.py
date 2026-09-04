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

patched_display_text = display_text
if old_test in patched_display_text:
    patched_display_text = patched_display_text.replace(old_test, new_test)

# The vendor demo paints 16 coloured startup bands. Pixels at the extreme
# circular aperture can survive later clipping and appear as a noisy halo.
# Initialise the whole GRAM uniformly black instead.
rainbow_pixel = "color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), EXAMPLE_LCD_COLOR_BITS) >> (k * 8)) & 0xff;"
black_pixel = "color[i * byte_per_pixel + k] = 0;"
patched_display_text = patched_display_text.replace(rainbow_pixel, black_pixel)

if patched_display_text != display_text:
    with open(display_source, "w", encoding="utf-8", newline="") as source_file:
        source_file.write(patched_display_text)

# The controller occasionally reports one isolated point while the panel is
# untouched.  Do the debounce at the LVGL input boundary: a contact must occur
# in two consecutive samples and remain in roughly the same place before it is
# allowed to generate a press.  Releasing remains immediate.
lvgl_source = join(driver_dir, "LVGL_Driver", "LVGL_Driver.c")
with open(lvgl_source, "r", encoding="utf-8") as source_file:
    lvgl_text = source_file.read()

old_touch_read = '''void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )
{
    uint16_t touchpad_x[5] = {0};
    uint16_t touchpad_y[5] = {0};
    uint8_t touchpad_cnt = 0;

    /* Get coordinates */
    bool touchpad_pressed = Touch_Get_xy( touchpad_x, touchpad_y, NULL, &touchpad_cnt, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);

    // printf("CCCCCCCCCCCCC=%d  \\r\\n",touchpad_cnt);
    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PR;
        // printf("X=%u Y=%u num=%d \\r\\n", data->point.x, data->point.y,touchpad_cnt);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

}'''
old_touch_read = old_touch_read.replace(
    "uint8_t touchpad_cnt = 0;\n\n", "uint8_t touchpad_cnt = 0;\n   \n"
).replace("    }\n\n}", "    }\n   \n}")

new_touch_read = '''void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )
{
    uint16_t touchpad_x[5] = {0};
    uint16_t touchpad_y[5] = {0};
    uint8_t touchpad_cnt = 0;
    static bool candidate_active;
    static uint16_t candidate_x;
    static uint16_t candidate_y;
    static uint8_t stable_samples;

    (void)drv;
    const bool touchpad_pressed = Touch_Get_xy(touchpad_x, touchpad_y, NULL,
                                               &touchpad_cnt,
                                               CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    const bool valid_point = touchpad_pressed && touchpad_cnt > 0 &&
                             touchpad_x[0] < EXAMPLE_LCD_WIDTH &&
                             touchpad_y[0] < EXAMPLE_LCD_HEIGHT;

    if (!valid_point) {
        candidate_active = false;
        stable_samples = 0;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    const int dx = (int)touchpad_x[0] - (int)candidate_x;
    const int dy = (int)touchpad_y[0] - (int)candidate_y;
    if (!candidate_active || dx > 24 || dx < -24 || dy > 24 || dy < -24) {
        candidate_active = true;
        candidate_x = touchpad_x[0];
        candidate_y = touchpad_y[0];
        stable_samples = 1;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if (stable_samples < 2) ++stable_samples;
    data->point.x = touchpad_x[0];
    data->point.y = touchpad_y[0];
    data->state = stable_samples == 2 ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}'''

if old_touch_read in lvgl_text:
    lvgl_text = lvgl_text.replace(old_touch_read, new_touch_read)
    with open(lvgl_source, "w", encoding="utf-8", newline="") as source_file:
        source_file.write(lvgl_text)
elif new_touch_read not in lvgl_text:
    raise RuntimeError("Could not apply Chronvs touch debounce patch")

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
