#include "services/rtc_service.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"

#include "I2C_Driver.h"

#define PCF85063_ADDRESS 0x51
#define PCF85063_TIME_REGISTER 0x04

static uint8_t bcd_to_decimal(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

chronvs_time_t chronvs_rtc_read(void) {
    const uint8_t register_start = PCF85063_TIME_REGISTER;
    uint8_t raw[7] = {0};
    chronvs_time_t time = {0};

    if (i2c_master_write_read_device(I2C_MASTER_NUM, PCF85063_ADDRESS,
                                     &register_start, 1, raw, sizeof(raw),
                                     pdMS_TO_TICKS(100)) != ESP_OK) {
        return time;
    }

    time.second = bcd_to_decimal(raw[0] & 0x7F);
    time.minute = bcd_to_decimal(raw[1] & 0x7F);
    time.hour = bcd_to_decimal(raw[2] & 0x3F);
    time.day = bcd_to_decimal(raw[3] & 0x3F);
    time.weekday = raw[4] & 0x07;
    time.month = bcd_to_decimal(raw[5] & 0x1F);
    time.year = bcd_to_decimal(raw[6]);
    time.valid = time.second < 60 && time.minute < 60 && time.hour < 24 &&
                 time.day >= 1 && time.day <= 31 && time.weekday < 7 &&
                 time.month >= 1 && time.month <= 12;
    return time;
}
