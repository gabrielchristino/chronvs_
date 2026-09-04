#pragma once

/*
 * Starts the background Wi-Fi/NTP synchronizer when credentials are present.
 * It writes local Sao Paulo time to the PCF85063 and powers Wi-Fi down between
 * synchronizations. Calling it without credentials is safe and does nothing.
 */
void chronvs_time_sync_start(void);
