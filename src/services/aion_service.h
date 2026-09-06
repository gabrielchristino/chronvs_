#pragma once

#include "services/rtc_service.h"

#define CHRONVS_ALARM_LIMIT 12
typedef struct { uint8_t hour, minute, days; } chronvs_alarm_t;

/* All calls belong to the main/LVGL task. Timings use monotonic microseconds. */
void chronvs_aion_init(void);
void chronvs_aion_observe_time(const chronvs_time_t *time);
void chronvs_aion_poll(void);
void chronvs_timer_start(uint32_t minutes);
void chronvs_timer_cancel(void);
uint32_t chronvs_timer_remaining(void);
bool chronvs_timer_running(void);
const chronvs_alarm_t *chronvs_alarm_get(unsigned index);
bool chronvs_alarm_create(uint8_t hour, uint8_t minute, uint8_t days);
bool chronvs_alarm_delete(unsigned index);
/* -2 = none, -1 = timer, 0..limit-1 = alarm. Pending alerts stay queued. */
int chronvs_aion_alert(void);
void chronvs_aion_dismiss(uint32_t extra_minutes);
