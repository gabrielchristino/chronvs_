#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "apps/app_catalog.h"
#include "apps/watch_app.h"
#include "core/app_manager.h"
#include "platform/board.h"
#include "services/battery_service.h"
#include "services/rtc_service.h"
#include "services/time_sync_service.h"
#include "ui/system_ui.h"
#include "services/aion_service.h"
#include "ui/aion_alert.h"

#define BATTERY_UPDATE_PERIOD_MS 60000

static const char *TAG = "chronvs";

void app_main(void) {
    ESP_LOGI(TAG, "Chronvs application runtime starting");
    chronvs_board_init();
    chronvs_battery_init();

    chronvs_app_manager_init(lv_scr_act());
    if (!chronvs_apps_register_all() || !chronvs_app_open("watch")) {
        ESP_LOGE(TAG, "Could not start the watch app");
    }
    chronvs_aion_init();
    chronvs_time_sync_start();

    TickType_t next_rtc_update = 0;
    TickType_t next_battery_update = 0;
    bool display_was_off = false;

    while (true) {
        chronvs_time_t synchronized_time;
        if (chronvs_time_sync_take_update(&synchronized_time))
            chronvs_aion_observe_time(&synchronized_time);
        const TickType_t now = xTaskGetTickCount();
        const bool display_is_off = chronvs_system_ui_display_is_off();
        if (display_is_off) {
            display_was_off = true;
        } else {
            const bool refresh_after_wake = display_was_off;
            if (refresh_after_wake || now >= next_rtc_update) {
                const chronvs_time_t time = chronvs_rtc_read();
                chronvs_aion_observe_time(&time);
                chronvs_watch_app_set_time(&time);
                next_rtc_update = now + pdMS_TO_TICKS(1000);
            }
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

        chronvs_aion_poll();
        chronvs_aion_alert_poll();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
