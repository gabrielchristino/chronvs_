#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "Display_SPD2010.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"

static const char *TAG = "chronvs";

typedef struct {
    uint8_t second, minute, hour, day, month, year;
    bool valid;
} clock_time_t;

static lv_obj_t *time_label;
static lv_obj_t *date_label;
static lv_obj_t *status_label;

static uint8_t bcd_to_decimal(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

static void scan_onboard_i2c(void) {
    static const uint8_t addresses[] = {0x20, 0x51, 0x53, 0x6A, 0x6B};
    static const char *names[] = {"TCA9554", "PCF85063 RTC", "SPD2010 touch", "QMI8658 IMU", "QMI8658 IMU"};
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        esp_err_t result = i2c_master_write_to_device(I2C_MASTER_NUM, addresses[i], NULL, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "I2C 0x%02X %-14s %s", addresses[i], names[i], result == ESP_OK ? "OK" : "no response");
    }
}

static clock_time_t read_rtc(void) {
    const uint8_t register_start = 0x04; // PCF85063: seconds through year
    uint8_t raw[7] = {0};
    clock_time_t time = {0};
    if (i2c_master_write_read_device(I2C_MASTER_NUM, 0x51, &register_start, 1, raw, sizeof(raw), pdMS_TO_TICKS(100)) != ESP_OK) return time;
    time.second = bcd_to_decimal(raw[0] & 0x7F); time.minute = bcd_to_decimal(raw[1] & 0x7F);
    time.hour = bcd_to_decimal(raw[2] & 0x3F); time.day = bcd_to_decimal(raw[3] & 0x3F);
    time.month = bcd_to_decimal(raw[5] & 0x1F); time.year = bcd_to_decimal(raw[6]);
    time.valid = time.second < 60 && time.minute < 60 && time.hour < 24 && time.day >= 1 && time.day <= 31 && time.month >= 1 && time.month <= 12;
    return time;
}

static void create_clock_screen(void) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050B18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "CHRONVS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x42E1FF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    time_label = lv_label_create(screen);
    lv_label_set_text(time_label, "--:--");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xF4FAFF), LV_PART_MAIN);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -35);

    date_label = lv_label_create(screen);
    lv_label_set_text(date_label, "--/--/----");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xFFB74D), LV_PART_MAIN);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 38);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "RTC");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x4D6B83), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -48);
}

static void update_clock_screen(const clock_time_t *time) {
    if (time->valid) {
        lv_label_set_text_fmt(time_label, "%02u:%02u", time->hour, time->minute);
        lv_label_set_text_fmt(date_label, "%02u/%02u/20%02u", time->day, time->month, time->year);
        lv_label_set_text_fmt(status_label, "%02u segundos", time->second);
    } else {
        lv_label_set_text(time_label, "--:--");
        lv_label_set_text(date_label, "RTC sem hora");
        lv_label_set_text(status_label, "Ajuste pendente");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Chronvs LVGL clock starting");
    I2C_Init();
    EXIO_Init();
    scan_onboard_i2c();
    LCD_Init();
    LVGL_Init();
    create_clock_screen();

    TickType_t next_rtc_update = 0;
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if (now >= next_rtc_update) {
            const clock_time_t time = read_rtc();
            update_clock_screen(&time);
            next_rtc_update = now + pdMS_TO_TICKS(250);
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
