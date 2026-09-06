#pragma once
#include "services/rtc_service.h"

/*
 * Starts the background Wi-Fi/NTP synchronizer when credentials are present.
 * It writes local Sao Paulo time to the PCF85063 and powers Wi-Fi down between
 * synchronizations. Calling it without credentials is safe and does nothing.
 */
void chronvs_time_sync_start(void);

/* Main task consumes NTP corrections even with the backlight off, without I2C. */
bool chronvs_time_sync_take_update(chronvs_time_t *time);
