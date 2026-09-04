#pragma once

#include "core/app_manager.h"
#include "services/rtc_service.h"

extern const chronvs_app_t chronvs_watch_app;

void chronvs_watch_app_set_time(const chronvs_time_t *time);
void chronvs_watch_app_set_ambient_temperature(float temperature_c);
