#pragma once
#include "services/rtc_service.h"

/*
 * Starts the background Wi-Fi/NTP synchronizer when credentials are present.
 * It writes local Sao Paulo time to the PCF85063 and powers Wi-Fi down between
 * synchronizations. Calling it without credentials is safe and does nothing.
 * Initialize wifi_session_service first on the main task.
 */
void chronvs_time_sync_start(void);

/* Main/UI task: poll before deciding whether to sleep. Each session owns a
 * temporary task; the monotonic deadline includes time spent in light sleep. */
void chronvs_time_sync_poll(void);
bool chronvs_time_sync_active(void);
uint32_t chronvs_time_sync_next_wake_ms(uint32_t max_ms);

/* Main task consumes NTP corrections even with the backlight off, without I2C. */
bool chronvs_time_sync_take_update(chronvs_time_t *time);
