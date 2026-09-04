#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Display_SPD2010.h"
#include "Touch_SPD2010.h"

static const char *TAG = "chronvs";
static const uint8_t known_i2c_addresses[] = {0x20, 0x51, 0x53, 0x6A, 0x6B};

static void scan_onboard_i2c(void) {
    ESP_LOGI(TAG, "--- I2C onboard ---");
    for (size_t i = 0; i < sizeof(known_i2c_addresses); ++i) {
        const uint8_t address = known_i2c_addresses[i];
        const char *name = "unknown";
        if (address == 0x20) name = "TCA9554 GPIO expander";
        if (address == 0x51) name = "PCF85063 RTC";
        if (address == 0x53) name = "SPD2010 touch";
        if (address == 0x6A || address == 0x6B) name = "QMI8658 IMU";
        esp_err_t result = i2c_master_write_to_device(I2C_MASTER_NUM, address, NULL, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "0x%02X  %-22s %s", address, name, result == ESP_OK ? "OK" : "no response");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Waveshare ESP32-S3-Touch-LCD-1.46 bring-up");
    I2C_Init();
    EXIO_Init();
    scan_onboard_i2c();

    LCD_Init();
    ESP_LOGI(TAG, "SPD2010 QSPI initialized; color test pattern sent");

    while (true) {
        uint16_t x = 0, y = 0;
        uint8_t points = 0;
        if (Touch_Get_xy(&x, &y, NULL, &points, 1) && points > 0) {
            ESP_LOGI(TAG, "touch: x=%u y=%u", x, y);
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
