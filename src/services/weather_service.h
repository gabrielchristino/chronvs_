#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { CHRONVS_WEATHER_IDLE, CHRONVS_WEATHER_FETCHING,
               CHRONVS_WEATHER_SUCCESS, CHRONVS_WEATHER_ERROR }
               chronvs_weather_state_t;
typedef struct {
    float temperature_c, apparent_temperature_c, minimum_c, maximum_c;
    uint8_t humidity_percent;
    int16_t weather_code;
    /* Local civil seconds since 1970-01-01, intentionally not UTC. */
    int64_t updated_epoch;
    bool valid;
} chronvs_weather_snapshot_t;

/* Call from the LVGL task; creates the lock and loads NVS once, without Wi-Fi. */
bool chronvs_weather_init(void);
bool chronvs_weather_get_snapshot(chronvs_weather_snapshot_t *snapshot);
chronvs_weather_state_t chronvs_weather_state(void);
/* Immutable message; safe to retain across worker updates. */
const char *chronvs_weather_error(void);
bool chronvs_weather_request_update(void);
bool chronvs_weather_take_result(chronvs_weather_snapshot_t *snapshot,
                                 char *error, size_t error_size);
