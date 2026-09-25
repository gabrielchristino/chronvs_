#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "apps/app_catalog.h"
#include "core/app_manager.h"
#include "platform/board.h"
#include "platform/display_profile.h"
#include "services/battery_service.h"
#include "services/rtc_service.h"
#include "services/time_sync_service.h"
#include "services/wifi_session_service.h"
#include "services/voice_lab_service.h"
#include "ui/system_ui.h"
#include "services/Relogio_service.h"
#include "ui/Relogio_alert.h"

#define BATTERY_UPDATE_PERIOD_MS 60000
#define DISPLAY_OFF_MAX_SLEEP_MS 300000

static const char *TAG = "chronvs";

void app_main(void) {
    ESP_LOGI(TAG, "Chronvs application runtime starting");
    chronvs_board_init();
    chronvs_display_profile_init();
    chronvs_battery_init();

    chronvs_app_manager_init(lv_scr_act());
    if (!chronvs_apps_register_all() || !chronvs_app_open("watch")) {
        ESP_LOGE(TAG, "Could not start the watch app");
    }
    chronvs_Relogio_init();
    chronvs_wifi_session_init();
    chronvs_time_sync_start();

    TickType_t next_battery_update = 0;
    bool display_was_off = false;
    /* At 100 Hz, pdMS_TO_TICKS(5) is zero: always block for at least one tick. */
    const TickType_t ui_delay = pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1;

    while (true) {
        chronvs_time_sync_poll();
        chronvs_time_t synchronized_time;
        if (chronvs_time_sync_take_update(&synchronized_time))
            chronvs_Relogio_observe_time(&synchronized_time);
        TickType_t now = xTaskGetTickCount();
        const bool display_is_off = chronvs_system_ui_display_is_off();
        chronvs_rtc_refresh(display_is_off);
        if (display_is_off) {
            if (!display_was_off)
                ESP_LOGI(TAG, "Display off: light sleep enabled");
            display_was_off = true;
            /* AUTO/ECO stops capture; let the worker release I2S/model before sleep. */
            if (chronvs_voice_lab_active()) chronvs_voice_lab_stop();
            if (!chronvs_wifi_session_active() && !chronvs_voice_lab_active() &&
                !chronvs_time_sync_active()) {
                chronvs_board_light_sleep(
                    chronvs_time_sync_next_wake_ms(
                        chronvs_Relogio_next_wake_ms(DISPLAY_OFF_MAX_SLEEP_MS)));
                now = xTaskGetTickCount();
            }
        } else {
            const bool refresh_after_wake = display_was_off;
            if (refresh_after_wake || now >= next_battery_update) {
                const chronvs_battery_status_t battery = chronvs_battery_read();
                if (battery.valid) {
                    chronvs_system_ui_set_battery(battery.percent, battery.voltage);
                } else {
                    ESP_LOGW(TAG, "Battery ADC did not return a valid voltage");
                }
                next_battery_update = now + pdMS_TO_TICKS(BATTERY_UPDATE_PERIOD_MS);
            }
            display_was_off = false;
        }

        chronvs_Relogio_poll();
        chronvs_Relogio_alert_poll();
        lv_timer_handler();
        chronvs_display_profile_poll(chronvs_system_ui_display_is_off());
        vTaskDelay(ui_delay);
    }
}
