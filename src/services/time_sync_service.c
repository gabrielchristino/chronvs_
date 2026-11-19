#include "services/time_sync_service.h"
#include "services/wifi_session_service.h"
#include "services/gps_service.h"
#include "services/Relogio_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C_Driver.h"

#define NTP_SYNC_TIMEOUT_MS 15000
#define SYNC_PERIOD_MS (12 * 60 * 60 * 1000)
#define TASK_RETRY_MS 60000
#define PCF85063_ADDRESS 0x51
#define PCF85063_TIME_REGISTER 0x04

static const char *TAG = "time_sync";
static portMUX_TYPE update_lock = portMUX_INITIALIZER_UNLOCKED;
static chronvs_time_t synchronized_time;
static bool update_pending;
static bool scheduler_started, worker_active;
static int64_t next_sync_us;
static int64_t last_ntp_us = -1;
static int64_t gps_retry_us, gps_window_us, gps_last_sync_us = -1;
static bool gps_searching, gps_rtc_pending;
static int64_t gps_anchor_utc, gps_anchor_us;
static int64_t gps_rtc_retry_us;

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
                last_ntp_us = esp_timer_get_time();
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
    synchronize_once();
    portENTER_CRITICAL(&update_lock);
    next_sync_us = esp_timer_get_time() + (int64_t)SYNC_PERIOD_MS * 1000;
    worker_active = false;
    portEXIT_CRITICAL(&update_lock);
    /* Wi-Fi/SNTP are already stopped; idle task reclaims this task's stack. */
    vTaskDelete(NULL);
}

void chronvs_time_sync_start(void) {
    if (scheduler_started) return;
    if (!chronvs_wifi_session_configured()) {
        ESP_LOGI(TAG, "No Wi-Fi credentials; using RTC with GPS time fallback");
        return;
    }
    scheduler_started = true;
    chronvs_time_sync_poll();
}

void chronvs_time_sync_poll(void) {
    if (!scheduler_started) return;
    portENTER_CRITICAL(&update_lock);
    bool due = !worker_active && esp_timer_get_time() >= next_sync_us;
    if (due) worker_active = true;
    portEXIT_CRITICAL(&update_lock);
    if (!due) return;
    BaseType_t created = xTaskCreate(time_sync_task, "time_sync", 6144,
                                     NULL, 4, NULL);
    if (created != pdPASS) {
        portENTER_CRITICAL(&update_lock);
        next_sync_us = esp_timer_get_time() + (int64_t)TASK_RETRY_MS * 1000;
        worker_active = false;
        portEXIT_CRITICAL(&update_lock);
        ESP_LOGE(TAG, "Could not create time synchronization task");
    }
}

bool chronvs_time_sync_active(void) {
    portENTER_CRITICAL(&update_lock);
    bool active = worker_active;
    portEXIT_CRITICAL(&update_lock);
    return active;
}

bool chronvs_time_sync_utc(int64_t *utc) {
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&update_lock);
    int64_t ntp=last_ntp_us;
    bool busy=worker_active;
    portEXIT_CRITICAL(&update_lock);
    bool recent_ntp=ntp>=0 && now-ntp<(int64_t)SYNC_PERIOD_MS*1000;
    bool recent_gps=gps_last_sync_us>=0 && now-gps_last_sync_us<(int64_t)SYNC_PERIOD_MS*1000;
    if (!utc || busy || (!recent_ntp && !recent_gps)) return false;
    *utc=(int64_t)time(NULL);
    return *utc>=1483228800LL;
}

uint32_t chronvs_time_sync_next_wake_ms(uint32_t max_ms) {
    if (!scheduler_started || !max_ms) return max_ms;
    portENTER_CRITICAL(&update_lock);
    bool active = worker_active;
    int64_t remaining_us = next_sync_us - esp_timer_get_time();
    portEXIT_CRITICAL(&update_lock);
    if (active) return max_ms;
    if (remaining_us <= 0) return 1;
    int64_t remaining_ms = (remaining_us + 999) / 1000;
    return remaining_ms < max_ms ? (uint32_t)remaining_ms : max_ms;
}

void chronvs_time_sync_gps_poll(bool display_off) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&update_lock);
    bool ntp_busy = worker_active;
    bool ntp_recent = last_ntp_us >= 0 && now-last_ntp_us < (int64_t)SYNC_PERIOD_MS*1000;
    portEXIT_CRITICAL(&update_lock);
    if (ntp_busy || ntp_recent) {
        chronvs_gps_clock_request(false); gps_searching = false;
        if (ntp_recent) gps_rtc_pending = false;
        return;
    }
    bool gps_due = gps_last_sync_us < 0 || now-gps_last_sync_us >= (int64_t)SYNC_PERIOD_MS*1000;
    if (gps_due) {
        if (!gps_searching && now >= gps_retry_us) {
            chronvs_gps_clock_request(true); gps_searching = true;
            gps_window_us = now + 120000000LL;
        }
        chronvs_gps_snapshot_t gps; chronvs_gps_snapshot(&gps);
        if (gps.running && gps.data.fix && gps.data.utc_valid &&
            (uint64_t)(now/1000)-gps.data.utc_ms < 2000) {
            gps_anchor_utc = gps.data.utc; gps_anchor_us = (int64_t)gps.data.utc_ms*1000;
            struct timeval tv = {.tv_sec = (time_t)(gps_anchor_utc + (now-gps_anchor_us)/1000000),
                .tv_usec = (long)((now-gps_anchor_us)%1000000)};
            if (settimeofday(&tv, NULL) == 0) {
                gps_last_sync_us = now; gps_rtc_pending = true; gps_rtc_retry_us = 0;
                chronvs_gps_clock_request(false); gps_searching = false;
            }
        }
        if (gps_searching && now >= gps_window_us) {
            chronvs_gps_clock_request(false); gps_searching = false;
            gps_retry_us = now + 3600000000LL;
        }
    }
    /* Never touch RTC/I2C or render with backlight off. Keep the elapsed UTC
     * anchor so a delayed write uses current time, including midnight rollover. */
    if (gps_rtc_pending && !display_off && now >= gps_rtc_retry_us) {
        time_t local = (time_t)(gps_anchor_utc + (now-gps_anchor_us)/1000000 - 3*3600);
        struct tm tm;
        if (gmtime_r(&local, &tm) && write_rtc(&tm) == ESP_OK) {
            chronvs_time_t sample = {.year = tm.tm_year-100, .month = tm.tm_mon+1,
                .day = tm.tm_mday, .weekday = tm.tm_wday, .hour = tm.tm_hour,
                .minute = tm.tm_min, .second = tm.tm_sec, .valid = true};
            chronvs_Relogio_observe_time(&sample);
            gps_rtc_pending = false;
        }
        else gps_rtc_retry_us = now + 60000000LL;
    }
}
