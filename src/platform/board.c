#include "platform/board.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "Display_SPD2010.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"

static const char *TAG = "board";

static void scan_onboard_i2c(void) {
    static const uint8_t addresses[] = {0x20, 0x51, 0x53, 0x6A, 0x6B};
    static const char *names[] = {
        "TCA9554", "PCF85063 RTC", "SPD2010 touch",
        "QMI8658 IMU", "QMI8658 IMU"
    };
    const uint8_t probe = 0;

    for (size_t index = 0; index < sizeof(addresses); ++index) {
        const esp_err_t result = i2c_master_write_to_device(
            I2C_MASTER_NUM, addresses[index], &probe, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "I2C 0x%02X %-14s %s", addresses[index], names[index],
                 result == ESP_OK ? "OK" : "no response");
    }
}

void chronvs_board_init(void) {
    I2C_Init();
    EXIO_Init();
    scan_onboard_i2c();
    LCD_Init();
    LVGL_Init();
}
