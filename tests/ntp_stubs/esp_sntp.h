#pragma once
#define SNTP_OPMODE_POLL 0
#define SNTP_SYNC_MODE_IMMED 0
#define SNTP_SYNC_STATUS_COMPLETED 1
void esp_sntp_setoperatingmode(int);
void esp_sntp_set_sync_mode(int);
void esp_sntp_setservername(int, const char *);
void esp_sntp_init(void);
void esp_sntp_stop(void);
int esp_sntp_get_sync_status(void);
