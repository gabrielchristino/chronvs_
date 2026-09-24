#pragma once

#include "services/rtc_service.h"

#define CHRONVS_ALARM_LIMIT 12
typedef struct { uint8_t hour, minute, days; } chronvs_alarm_t;

/* All calls belong to the main/LVGL task. Timings use monotonic microseconds. */
void chronvs_Relogio_init(void);
void chronvs_Relogio_observe_time(const chronvs_time_t *time);
void chronvs_Relogio_poll(void);
/* Earliest pending timer, snooze, alarm or reminder, capped by max_ms. */
uint32_t chronvs_Relogio_next_wake_ms(uint32_t max_ms);
void chronvs_timer_start(uint32_t minutes);
void chronvs_timer_cancel(void);
uint32_t chronvs_timer_remaining(void);
bool chronvs_timer_running(void);
/* Stopwatch advances during light sleep and while its app is hidden. */
void chronvs_stopwatch_start(void);
void chronvs_stopwatch_pause(void);
void chronvs_stopwatch_reset(void);
bool chronvs_stopwatch_running(void);
uint64_t chronvs_stopwatch_elapsed_ms(void);
const chronvs_alarm_t *chronvs_alarm_get(unsigned index);
bool chronvs_alarm_create(uint8_t hour, uint8_t minute, uint8_t days);
bool chronvs_alarm_delete(unsigned index);
/* -2 = none, -1 = timer, 0..limit-1 = alarm. Pending alerts stay queued. */
int chronvs_Relogio_alert(void);
void chronvs_Relogio_dismiss(uint32_t extra_minutes);

#define CHRONVS_REMINDER_LIMIT 12
#define CHRONVS_REMINDER_CHARS 40
#define CHRONVS_REMINDER_BYTES (CHRONVS_REMINDER_CHARS * 2 + 1)
typedef struct {
    uint8_t year, month, day, hour, minute, done;
    char title[CHRONVS_REMINDER_BYTES];
} chronvs_reminder_t;
/* Dated, one-shot reminders share the clock and alert queue. IDs in the alert
 * queue start at CHRONVS_ALARM_LIMIT. Overdue reminders alert after reboot too. */
const chronvs_reminder_t *chronvs_reminder_get(unsigned index);
bool chronvs_reminder_create(const chronvs_reminder_t *reminder);
bool chronvs_reminder_delete(unsigned index);
bool chronvs_reminder_complete(unsigned index);
bool chronvs_Relogio_time(chronvs_time_t *time);
uint32_t chronvs_reminder_revision(void);
