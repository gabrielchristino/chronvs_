#include "services/time_sync_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "I2C_Driver.h"

#if __has_include("chronvs_secrets.h")
#include "chronvs_secrets.h"
#endif

#ifndef CHRONVS_WIFI_SSID
#define CHRONVS_WIFI_SSID ""
#endif

#ifndef CHRONVS_WIFI_PASSWORD
#define CHRONVS_WIFI_PASSWORD ""
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAX_RETRIES 5
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define NTP_SYNC_TIMEOUT_MS 15000
#define SYNC_PERIOD_MS (12 * 60 * 60 * 1000)
#define PCF85063_ADDRESS 0x51
#define PCF85063_TIME_REGISTER 0x04

static const char *TAG = "time_sync";
static EventGroupHandle_t wifi_events;
static volatile bool connection_requested;
static int connection_retries;
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

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    (void)argument;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (connection_requested) esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!connection_requested) return;

        if (connection_retries < WIFI_MAX_RETRIES) {
            ++connection_retries;
            ESP_LOGW(TAG, "Wi-Fi disconnected; retry %d/%d",
                     connection_retries, WIFI_MAX_RETRIES);
            esp_wifi_connect();
        }
        else {
            xEventGroupSetBits(wifi_events, WIFI_FAILED_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        connection_retries = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
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
    connection_retries = 0;
    connection_requested = true;
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    esp_err_t result = esp_wifi_start();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wi-Fi: %s", esp_err_to_name(result));
        connection_requested = false;
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    bool rtc_updated = false;
    if (bits & WIFI_CONNECTED_BIT) {
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
    else {
        ESP_LOGW(TAG, "Wi-Fi connection timed out");
    }

    connection_requested = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    return rtc_updated;
}

static esp_err_t initialize_wifi(void) {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Could not erase NVS");
        result = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(result, TAG, "Could not initialize NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Could not initialize network stack");

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) return ESP_ERR_NO_MEM;

    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&initialization), TAG, "Could not initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Could not select Wi-Fi storage");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, NULL, NULL),
        TAG, "Could not register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_event_handler, NULL, NULL),
        TAG, "Could not register IP event handler");

    wifi_config_t configuration = {0};
    strlcpy((char *)configuration.sta.ssid, CHRONVS_WIFI_SSID,
            sizeof(configuration.sta.ssid));
    strlcpy((char *)configuration.sta.password, CHRONVS_WIFI_PASSWORD,
            sizeof(configuration.sta.password));
    configuration.sta.threshold.authmode = WIFI_AUTH_OPEN;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Could not set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &configuration),
                        TAG, "Could not configure station");
    return ESP_OK;
}

static void time_sync_task(void *argument) {
    (void)argument;

    esp_err_t result = initialize_wifi();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Time synchronization disabled: %s", esp_err_to_name(result));
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        synchronize_once();
        vTaskDelay(pdMS_TO_TICKS(SYNC_PERIOD_MS));
    }
}

void chronvs_time_sync_start(void) {
    if (CHRONVS_WIFI_SSID[0] == '\0') {
        ESP_LOGI(TAG, "No Wi-Fi credentials; using PCF85063 only");
        return;
    }

    BaseType_t created = xTaskCreate(time_sync_task, "time_sync", 6144,
                                     NULL, 4, NULL);
    if (created != pdPASS) ESP_LOGE(TAG, "Could not create time synchronization task");
}
