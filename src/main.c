#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Display_SPD2010.h"
#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Touch_SPD2010.h"

#define SCREEN_WIDTH 412
#define SCREEN_HEIGHT 412
#define BAND_HEIGHT 16
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const char *TAG = "chronvs";
typedef struct { uint8_t second, minute, hour, day, month, year; bool valid; } clock_time_t;

static uint8_t bcd_to_decimal(uint8_t value) { return ((value >> 4) * 10) + (value & 0x0F); }

static void scan_onboard_i2c(void) {
    static const uint8_t addresses[] = {0x20, 0x51, 0x53, 0x6A, 0x6B};
    static const char *names[] = {"TCA9554 GPIO expander", "PCF85063 RTC", "SPD2010 touch", "QMI8658 IMU", "QMI8658 IMU"};
    ESP_LOGI(TAG, "--- I2C onboard ---");
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        esp_err_t result = i2c_master_write_to_device(I2C_MASTER_NUM, addresses[i], NULL, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "0x%02X  %-22s %s", addresses[i], names[i], result == ESP_OK ? "OK" : "no response");
    }
}

static clock_time_t read_rtc(void) {
    // PCF85063: registers 0x04..0x0A contain second through year in BCD.
    const uint8_t register_start = 0x04;
    uint8_t raw[7] = {0};
    clock_time_t time = {0};
    if (i2c_master_write_read_device(I2C_MASTER_NUM, 0x51, &register_start, 1, raw, sizeof(raw), pdMS_TO_TICKS(100)) != ESP_OK) return time;
    time.second = bcd_to_decimal(raw[0] & 0x7F); time.minute = bcd_to_decimal(raw[1] & 0x7F);
    time.hour = bcd_to_decimal(raw[2] & 0x3F); time.day = bcd_to_decimal(raw[3] & 0x3F);
    time.month = bcd_to_decimal(raw[5] & 0x1F); time.year = bcd_to_decimal(raw[6]);
    time.valid = time.second < 60 && time.minute < 60 && time.hour < 24 && time.day >= 1 && time.day <= 31 && time.month >= 1 && time.month <= 12;
    return time;
}

static void rect(uint16_t *band, int band_y, int x, int y, int width, int height, uint16_t color) {
    const int y0 = y > band_y ? y : band_y;
    const int y1 = (y + height) < (band_y + BAND_HEIGHT) ? y + height : band_y + BAND_HEIGHT;
    const int x0 = x > 0 ? x : 0, x1 = (x + width) < SCREEN_WIDTH ? x + width : SCREEN_WIDTH;
    for (int py = y0; py < y1 && py < SCREEN_HEIGHT; ++py)
        for (int px = x0; px < x1; ++px) band[(py - band_y) * SCREEN_WIDTH + px] = color;
}

static void digit(uint16_t *band, int band_y, int x, int y, int scale, uint8_t value, uint16_t color) {
    static const uint8_t segments[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const int length = 9 * scale, thick = 2 * scale;
    const uint8_t s = value < 10 ? segments[value] : 0;
    if (s & 0x01) rect(band, band_y, x + thick, y, length, thick, color);
    if (s & 0x02) rect(band, band_y, x + length + thick, y + thick, thick, length, color);
    if (s & 0x04) rect(band, band_y, x + length + thick, y + length + 2 * thick, thick, length, color);
    if (s & 0x08) rect(band, band_y, x + thick, y + 2 * length + 2 * thick, length, thick, color);
    if (s & 0x10) rect(band, band_y, x, y + length + 2 * thick, thick, length, color);
    if (s & 0x20) rect(band, band_y, x, y + thick, thick, length, color);
    if (s & 0x40) rect(band, band_y, x + thick, y + length + thick, length, thick, color);
}

static void colon(uint16_t *band, int band_y, int x, int y, int scale, uint16_t color) {
    rect(band, band_y, x, y + 7 * scale, 2 * scale, 2 * scale, color);
    rect(band, band_y, x, y + 15 * scale, 2 * scale, 2 * scale, color);
}

static void paint_band(uint16_t *band, int band_y, const clock_time_t *time, bool touched) {
    const uint16_t blue = RGB565(66, 225, 255), orange = RGB565(255, 183, 77), dim = RGB565(29, 75, 98);
    memset(band, 0, SCREEN_WIDTH * BAND_HEIGHT * sizeof(uint16_t));
    rect(band, band_y, 38, 45, 336, 2, dim); rect(band, band_y, 38, 365, 336, 2, dim);
    rect(band, band_y, 80, 78, 2, 256, dim); rect(band, band_y, 330, 78, 2, 256, dim);
    const uint8_t values[] = {time->hour / 10, time->hour % 10, time->minute / 10, time->minute % 10};
    const int x[] = {55, 116, 214, 275};
    for (int i = 0; i < 4; ++i) digit(band, band_y, x[i], 105, 5, values[i], blue);
    colon(band, band_y, 190, 105, 5, orange);
    digit(band, band_y, 115, 258, 2, time->day / 10, orange); digit(band, band_y, 141, 258, 2, time->day % 10, orange);
    rect(band, band_y, 169, 281, 5, 4, dim);
    digit(band, band_y, 184, 258, 2, time->month / 10, orange); digit(band, band_y, 210, 258, 2, time->month % 10, orange);
    rect(band, band_y, 238, 281, 5, 4, dim);
    digit(band, band_y, 253, 258, 2, 2, orange); digit(band, band_y, 279, 258, 2, 0, orange);
    digit(band, band_y, 305, 258, 2, time->year / 10, orange); digit(band, band_y, 331, 258, 2, time->year % 10, orange);
    digit(band, band_y, 179, 320, 2, time->second / 10, blue); digit(band, band_y, 205, 320, 2, time->second % 10, blue);
    rect(band, band_y, 198, 353, 16, 4, touched ? orange : dim);
}

static void render_clock(const clock_time_t *time, bool touched) {
    static uint16_t *band;
    if (!band) band = heap_caps_malloc(SCREEN_WIDTH * BAND_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!band || !panel_handle) return;
    for (int y = 0; y < SCREEN_HEIGHT; y += BAND_HEIGHT) {
        paint_band(band, y, time, touched);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, SCREEN_WIDTH, y + BAND_HEIGHT > SCREEN_HEIGHT ? SCREEN_HEIGHT : y + BAND_HEIGHT, band));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Chronvs clock starting");
    I2C_Init(); EXIO_Init(); scan_onboard_i2c(); LCD_Init();
    while (true) {
        uint16_t x = 0, y = 0; uint8_t points = 0;
        const bool touched = Touch_Get_xy(&x, &y, NULL, &points, 1) && points > 0;
        const clock_time_t time = read_rtc();
        if (!time.valid) ESP_LOGW(TAG, "RTC has no valid date/time yet");
        render_clock(&time, touched);
        if (touched) ESP_LOGI(TAG, "touch: x=%u y=%u", x, y);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
