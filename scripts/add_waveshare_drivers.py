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

# Keep the proven vendor flush flow.  Larger draw buffers and an asynchronous
# completion callback caused the SPD2010 panel to stall in practice.
vendor_flush = '''void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 +1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}'''

async_flush = '''bool example_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_io_event_data_t *edata,
                             void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_disp_drv_t *driver = (lv_disp_drv_t *)user_ctx;
    if (driver != NULL && driver->flush_cb != NULL) {
        lv_disp_flush_ready(driver);
    }
    return false;
}

void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_map);
}'''

if async_flush in lvgl_text:
    lvgl_text = lvgl_text.replace(async_flush, vendor_flush)
elif vendor_flush not in lvgl_text:
    raise RuntimeError("Could not restore Waveshare LVGL flush callback")

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
    uint16_t touchpad_strength[5] = {0};
    uint8_t touchpad_cnt = 0;
    static bool candidate_active;
    static bool contact_confirmed;
    static uint16_t candidate_x;
    static uint16_t candidate_y;
    static uint8_t stable_samples;

    (void)drv;
    const bool touchpad_pressed = Touch_Get_xy(touchpad_x, touchpad_y,
                                               touchpad_strength,
                                               &touchpad_cnt,
                                               CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    const bool valid_point = touchpad_pressed && touchpad_cnt > 0 &&
                             touchpad_strength[0] > 0 &&
                             touchpad_x[0] < EXAMPLE_LCD_WIDTH &&
                             touchpad_y[0] < EXAMPLE_LCD_HEIGHT;

    if (!valid_point) {
        candidate_active = false;
        contact_confirmed = false;
        stable_samples = 0;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if (!candidate_active) {
        candidate_active = true;
        candidate_x = touchpad_x[0];
        candidate_y = touchpad_y[0];
        stable_samples = 1;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    const int dx = (int)touchpad_x[0] - (int)candidate_x;
    const int dy = (int)touchpad_y[0] - (int)candidate_y;
    candidate_x = touchpad_x[0];
    candidate_y = touchpad_y[0];

    if (!contact_confirmed) {
        if (dx > 24 || dx < -24 || dy > 24 || dy < -24) {
            stable_samples = 1;
            data->state = LV_INDEV_STATE_REL;
            return;
        }
        if (stable_samples < 2) ++stable_samples;
        contact_confirmed = stable_samples == 2;
    }

    data->point.x = touchpad_x[0];
    data->point.y = touchpad_y[0];
    data->state = contact_confirmed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}'''

touch_read_start = lvgl_text.find("void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )")
touch_read_end_marker = "\n}\n/* Rotate display and touch"
touch_read_end = lvgl_text.find(touch_read_end_marker, touch_read_start)
if touch_read_start < 0 or touch_read_end < 0:
    raise RuntimeError("Could not locate Waveshare touch callback")

current_touch_read = lvgl_text[touch_read_start:touch_read_end + 2]
known_touch_read = current_touch_read == old_touch_read or "static bool candidate_active;" in current_touch_read
if current_touch_read != new_touch_read and known_touch_read:
    lvgl_text = (lvgl_text[:touch_read_start] + new_touch_read +
                 lvgl_text[touch_read_end + 2:])
elif current_touch_read != new_touch_read:
    raise RuntimeError("Could not apply Chronvs touch debounce patch")

with open(lvgl_source, "w", encoding="utf-8", newline="") as source_file:
    source_file.write(lvgl_text)

lvgl_header = join(driver_dir, "LVGL_Driver", "LVGL_Driver.h")
with open(lvgl_header, "r", encoding="utf-8") as header_file:
    header_text = header_file.read()

# The vendor's original buffer size is required by this SPD2010 QSPI driver.
# Larger buffers produce black bands on the physical display.
header_text = header_text.replace(
    "#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT)",
    "#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)")
header_text = header_text.replace(
    "#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 4)",
    "#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)")
header_text = header_text.replace(
    "#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 10)",
    "#define LVGL_BUF_LEN  (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)")
async_flush_prototype = '''bool example_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                             esp_lcd_panel_io_event_data_t *edata,
                             void *user_ctx);
'''
header_text = header_text.replace(async_flush_prototype, "")
with open(lvgl_header, "w", encoding="utf-8", newline="") as header_file:
    header_file.write(header_text)

with open(display_source, "r", encoding="utf-8") as source_file:
    display_text = source_file.read()

display_text = display_text.replace('#include "Display_SPD2010.h"\n#include "LVGL_Driver.h"',
                                    '#include "Display_SPD2010.h"')
async_io_callback = ".on_color_trans_done = example_lvgl_flush_ready,         \n    .user_ctx = &disp_drv,"
vendor_io_callback = ".on_color_trans_done = NULL,                            \n    .user_ctx = NULL,"
display_text = display_text.replace(async_io_callback, vendor_io_callback)
with open(display_source, "w", encoding="utf-8", newline="") as source_file:
    source_file.write(display_text)

display_header = join(driver_dir, "LCD_Driver", "Display_SPD2010.h")
with open(display_header, "r", encoding="utf-8") as header_file:
    display_header_text = header_file.read()

display_header_text = display_header_text.replace(
    "#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE   (8192)",
    "#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE   (2048)")
with open(display_header, "w", encoding="utf-8", newline="") as header_file:
    header_file.write(display_header_text)

# Keep flush_ready in the synchronous LVGL callback, but make draw_bitmap
# actually finish its queued QSPI writes before returning. IDF 5.3.1's SPI
# tx_param(-1, NULL, 0) drains pending color transactions without sending a
# command. This also protects the vendor startup clear's buffer before free().
# Set CHRONVS_QSPI_DRAIN=0 for an isolated comparison with the vendor behavior.
panel_source = join(driver_dir, "LCD_Driver", "esp_lcd_spd2010", "esp_lcd_spd2010.c")
with open(panel_source, "r", encoding="utf-8") as source_file:
    panel_text = source_file.read()
color_transfer = '    ESP_RETURN_ON_ERROR(tx_color(spd2010, io, LCD_CMD_RAMWR, color_data, len), TAG, "send color failed");'
drain_transfer = color_transfer + '''
    // Chronvs: finish queued pixel transfers before releasing their buffer.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, -1, NULL, 0), TAG, "drain color failed");'''
if drain_transfer in panel_text:
    panel_text = panel_text.replace(drain_transfer, color_transfer)
if panel_text.count(color_transfer) != 1:
    raise RuntimeError("Could not locate SPD2010 pixel transfer for synchronous drain")
if os.environ.get("CHRONVS_QSPI_DRAIN", "1") != "0":
    panel_text = panel_text.replace(color_transfer, drain_transfer)
    print("Chronvs: synchronous QSPI drain enabled (1/20 buffers, 2 KiB transfers)")
else:
    print("Chronvs: vendor QSPI completion behavior (diagnostic comparison)")
with open(panel_source, "r", encoding="utf-8") as source_file:
    original_panel_text = source_file.read()
if panel_text != original_panel_text:
    with open(panel_source, "w", encoding="utf-8", newline="") as source_file:
        source_file.write(panel_text)

env.Append(CPPPATH=[
    join(driver_dir, "I2C_Driver"),
    join(driver_dir, "EXIO"),
    join(driver_dir, "Touch_Driver"),
    join(driver_dir, "LCD_Driver"),
    join(driver_dir, "LCD_Driver", "esp_lcd_spd2010"),
    join(driver_dir, "LVGL_Driver"),
    join(driver_dir, "BAT_Driver"),
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
        "+<BAT_Driver/BAT_Driver.c>",
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
