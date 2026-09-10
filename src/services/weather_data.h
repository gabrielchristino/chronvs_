#pragma once

#include "services/weather_service.h"
#include "services/rtc_service.h"

#define CHRONVS_WEATHER_RESPONSE_LIMIT 16384
typedef enum { WEATHER_SUN, WEATHER_PARTLY_CLOUDY, WEATHER_CLOUD,
               WEATHER_FOG, WEATHER_RAIN, WEATHER_SNOW, WEATHER_STORM }
               chronvs_weather_icon_t;

bool chronvs_weather_parse(const char *json, size_t length,
                           chronvs_weather_snapshot_t *snapshot);
bool chronvs_weather_snapshot_valid(const chronvs_weather_snapshot_t *snapshot);
int64_t chronvs_weather_local_epoch(const chronvs_time_t *time);
const char *chronvs_weather_condition(int code);
chronvs_weather_icon_t chronvs_weather_icon(int code);
void chronvs_weather_age(const chronvs_weather_snapshot_t *snapshot,
                         int64_t now, char *text, size_t size);
