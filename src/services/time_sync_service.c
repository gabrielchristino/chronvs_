#include "services/time_sync_service.h"
#include "services/wifi_session_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C_Driver.h"

#define NTP_SYNC_TIMEOUT_MS 15000
#define SYNC_PERIOD_MS (12 * 60 * 60 * 1000)
#define PCF85063_ADDRESS 0x51
#define PCF85063_TIME_REGISTER 0x04

static const char *TAG = "time_sync";
static portMUX_TYPE update_lock = portMUX_INITIALIZER_UNLOCKED;
static chronvs_time_t synchronized_time;
static bool update_pending;

bool chronvs_time_sync_take_update(chronvs_time_t *time) {
    portENTER_CRITICAL(&update_lock);
    bool pending = update_pending;
    if (pending) { *time = synchronized_time; update_pending = false; }
    portEXIT_CRITICAL(&update_lock);
    return pending;
}

static uint8_t decimal_to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t write_rtc(const struct tm *local_time) {
    const uint8_t payload[] = {
        PCF85063_TIME_REGISTER,
        decimal_to_bcd((uint8_t)local_time->tm_sec),
        decimal_to_bcd((uint8_t)local_time->tm_min),
        decimal_to_bcd((uint8_t)local_time->tm_hour),
        decimal_to_bcd((uint8_t)local_time->tm_mday),
        (uint8_t)local_time->tm_wday,
        decimal_to_bcd((uint8_t)(local_time->tm_mon + 1)),
        decimal_to_bcd((uint8_t)((local_time->tm_year + 1900) % 100)),
    };

    return i2c_master_write_to_device(I2C_MASTER_NUM, PCF85063_ADDRESS,
                                      payload, sizeof(payload),
                                      pdMS_TO_TICKS(250));
}

static bool wait_for_ntp(struct tm *local_time) {
    /* ESP-IDF/newlib expects a POSIX TZ string. Brazil currently uses UTC-3. */
    setenv("TZ", "BRT3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    const int attempts = NTP_SYNC_TIMEOUT_MS / 500;
    bool synchronized = false;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            synchronized = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (synchronized) {
        time_t now;
        time(&now);
        synchronized = localtime_r(&now, local_time) != NULL;
    }

    esp_sntp_stop();
    return synchronized;
}

static bool synchronize_once(void) {
    esp_err_t result = chronvs_wifi_session_acquire();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi session unavailable: %s", esp_err_to_name(result));
        return false;
    }
    bool rtc_updated = false;
    {
        struct tm local_time = {0};
        if (wait_for_ntp(&local_time)) {
            result = write_rtc(&local_time);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "RTC synchronized: %04d-%02d-%02d %02d:%02d:%02d BRT",
                         local_time.tm_year + 1900, local_time.tm_mon + 1,
                         local_time.tm_mday, local_time.tm_hour,
                         local_time.tm_min, local_time.tm_sec);
                rtc_updated = true;
                portENTER_CRITICAL(&update_lock);
                synchronized_time = (chronvs_time_t){
                    .year = (local_time.tm_year + 1900) % 100,
                    .month = local_time.tm_mon + 1, .day = local_time.tm_mday,
                    .weekday = local_time.tm_wday, .hour = local_time.tm_hour,
                    .minute = local_time.tm_min, .second = local_time.tm_sec, .valid = true,
                };
                update_pending = true;
                portEXIT_CRITICAL(&update_lock);
            }
            else {
                ESP_LOGE(TAG, "Could not write PCF85063: %s", esp_err_to_name(result));
            }
        }
        else {
            ESP_LOGW(TAG, "NTP synchronization timed out");
        }
    }

    chronvs_wifi_session_release();
    return rtc_updated;
}

static void time_sync_task(void *argument) {
    (void)argument;

    while (true) {
        synchronize_once();
        vTaskDelay(pdMS_TO_TICKS(SYNC_PERIOD_MS));
    }
}

void chronvs_time_sync_start(void) {
    if (!chronvs_wifi_session_configured()) {
        ESP_LOGI(TAG, "No Wi-Fi credentials; using PCF85063 only");
        return;
    }

    BaseType_t created = xTaskCreate(time_sync_task, "time_sync", 6144,
                                     NULL, 4, NULL);
    if (created != pdPASS) ESP_LOGE(TAG, "Could not create time synchronization task");
}
