#include "services/Relogio_service.h"

#include <string.h>
#include "esp_timer.h"
#include "nvs.h"
#include "core/calendar.h"
#include "core/Notas_text.h"

static chronvs_alarm_t alarms[CHRONVS_ALARM_LIMIT];
static int64_t last_day[CHRONVS_ALARM_LIMIT];
static int64_t snooze[CHRONVS_ALARM_LIMIT];
static bool pending[CHRONVS_ALARM_LIMIT], timer_pending;
static int64_t timer_deadline, anchor_us, anchor_seconds;
static bool clock_valid;
static nvs_handle_t storage;
static chronvs_reminder_t reminders[CHRONVS_REMINDER_LIMIT];
static bool reminder_pending[CHRONVS_REMINDER_LIMIT];
static uint32_t reminder_revision;

static bool reminder_valid(const chronvs_reminder_t *r) {
    if (!r || !chronvs_calendar_valid(2000 + r->year, r->month, r->day) ||
        r->hour > 23 || r->minute > 59 || r->done > 1 ||
        !Notas_text_valid(r->title, sizeof(r->title)) ||
        Notas_text_length(r->title) > CHRONVS_REMINDER_CHARS || strchr(r->title, '\n')) return false;
    return r->title[strspn(r->title, " ")] != 0;
}

static int64_t reminder_seconds(const chronvs_reminder_t *r) {
    return (int64_t)chronvs_calendar_ordinal(2000 + r->year, r->month, r->day) * 86400 +
        r->hour * 3600 + r->minute * 60;
}

static int64_t local_seconds(const chronvs_time_t *t) {
    static const uint16_t before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int days = 365 * t->year + (t->year + 3) / 4 + before_month[t->month - 1] + t->day - 1;
    if (t->year % 4 == 0 && t->month > 2) ++days;
    return (int64_t)days * 86400 + t->hour * 3600 + t->minute * 60 + t->second;
}

static int64_t clock_seconds(void) {
    return anchor_seconds + (esp_timer_get_time() - anchor_us) / 1000000;
}

void chronvs_Relogio_init(void) {
    ++reminder_revision;
    memset(alarms, 0, sizeof(alarms));
    memset(pending, 0, sizeof(pending));
    memset(snooze, 0, sizeof(snooze));
    memset(reminder_pending, 0, sizeof(reminder_pending));
    memset(reminders, 0, sizeof(reminders));
    clock_valid = timer_pending = false;
    timer_deadline = 0;
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) last_day[i] = -1;
    if (nvs_open("aion", NVS_READWRITE, &storage) != ESP_OK) return;
    size_t size = sizeof(alarms);
    if (nvs_get_blob(storage, "alarms_v1", alarms, &size) != ESP_OK || size != sizeof(alarms)) {
        memset(alarms, 0, sizeof(alarms));
    }
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
        if (alarms[i].hour > 23 || alarms[i].minute > 59 || alarms[i].days > 127)
            memset(&alarms[i], 0, sizeof(alarms[i]));
    }
    size = sizeof(reminders);
    if (nvs_get_blob(storage, "reminders_v1", reminders, &size) != ESP_OK || size != sizeof(reminders))
        memset(reminders, 0, sizeof(reminders));
    for (unsigned i = 0; i < CHRONVS_REMINDER_LIMIT; ++i)
        if (!reminder_valid(&reminders[i])) memset(&reminders[i], 0, sizeof(reminders[i]));
}

void chronvs_Relogio_observe_time(const chronvs_time_t *time) {
    if (!time || !time->valid || !chronvs_calendar_valid(2000 + time->year, time->month, time->day) ||
        time->hour > 23 || time->minute > 59 || time->second > 59) return;
    const int64_t seconds = local_seconds(time);
    /* Keep the subsecond anchor stable despite repeated integer RTC reads. */
    if (!clock_valid || seconds != clock_seconds()) {
        anchor_seconds = seconds;
        anchor_us = esp_timer_get_time();
        clock_valid = true;
    }
}

void chronvs_Relogio_poll(void) {
    int64_t now = esp_timer_get_time();
    if (timer_deadline && now >= timer_deadline) {
        timer_deadline = 0;
        timer_pending = true;
    }
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
        if (snooze[i] && now >= snooze[i]) { snooze[i] = 0; pending[i] = true; }
    }
    if (!clock_valid) return;
    int64_t seconds = clock_seconds();
    for (unsigned i = 0; i < CHRONVS_REMINDER_LIMIT; ++i)
        if (reminders[i].title[0] && !reminders[i].done && seconds >= reminder_seconds(&reminders[i]))
            reminder_pending[i] = true;
    int64_t day = seconds / 86400;
    unsigned weekday = (day + 6) % 7; /* 2000-01-01 was Saturday. */
    unsigned minute = (seconds % 86400) / 60;
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
        if ((alarms[i].days & (1 << weekday)) &&
            minute == alarms[i].hour * 60u + alarms[i].minute && last_day[i] != day) {
            last_day[i] = day;
            pending[i] = true;
        }
    }
}

uint32_t chronvs_Relogio_next_wake_ms(uint32_t max_ms) {
    if (!max_ms) return 0;
    const int64_t now_us = esp_timer_get_time();
    int64_t next_us = now_us + (int64_t)max_ms * 1000;

    if (timer_deadline && timer_deadline < next_us) next_us = timer_deadline;
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i)
        if (snooze[i] && snooze[i] < next_us) next_us = snooze[i];

    if (clock_valid) {
        const int64_t seconds = anchor_seconds + (now_us - anchor_us) / 1000000;
        const int64_t day_start = seconds - seconds % 86400;
        const unsigned weekday = (day_start / 86400 + 6) % 7;

        for (unsigned i = 0; i < CHRONVS_REMINDER_LIMIT; ++i) {
            if (!reminders[i].title[0] || reminders[i].done) continue;
            const int64_t due_seconds = reminder_seconds(&reminders[i]);
            const int64_t due_us = anchor_us + (due_seconds - anchor_seconds) * 1000000;
            if (due_us < next_us) next_us = due_us;
        }
        for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
            if (!alarms[i].days) continue;
            for (unsigned offset = 0; offset < 7; ++offset) {
                if (!(alarms[i].days & (1 << ((weekday + offset) % 7)))) continue;
                const int64_t due_seconds = day_start + (int64_t)offset * 86400 +
                                            alarms[i].hour * 3600 + alarms[i].minute * 60;
                if (due_seconds <= seconds) continue;
                const int64_t due_us = anchor_us + (due_seconds - anchor_seconds) * 1000000;
                if (due_us < next_us) next_us = due_us;
                break;
            }
        }
    }

    if (next_us <= now_us) return 1;
    return (uint32_t)((next_us - now_us + 999) / 1000);
}

void chronvs_timer_start(uint32_t minutes) {
    if (!minutes || minutes > 120) return;
    timer_pending = false;
    timer_deadline = esp_timer_get_time() + (int64_t)minutes * 60000000;
}
void chronvs_timer_cancel(void) { timer_deadline = 0; timer_pending = false; }
bool chronvs_timer_running(void) { return timer_deadline != 0; }
uint32_t chronvs_timer_remaining(void) {
    int64_t remaining = timer_deadline - esp_timer_get_time();
    return timer_deadline && remaining > 0 ? (remaining + 999999) / 1000000 : 0;
}
const chronvs_alarm_t *chronvs_alarm_get(unsigned index) {
    return index < CHRONVS_ALARM_LIMIT && alarms[index].days ? &alarms[index] : NULL;
}
static bool save(void) {
    return storage && nvs_set_blob(storage, "alarms_v1", alarms, sizeof(alarms)) == ESP_OK &&
           nvs_commit(storage) == ESP_OK;
}
bool chronvs_alarm_create(uint8_t hour, uint8_t minute, uint8_t days) {
    if (hour > 23 || minute > 59 || !days || days > 127) return false;
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
        if (alarms[i].days) continue;
        alarms[i] = (chronvs_alarm_t){hour, minute, days};
        if (!save()) { memset(&alarms[i], 0, sizeof(alarms[i])); return false; }
        /* A newly created alarm starts at the next occurrence. */
        last_day[i] = clock_valid && (clock_seconds() % 86400) / 60 == hour * 60 + minute
                          ? clock_seconds() / 86400 : -1;
        return true;
    }
    return false;
}
bool chronvs_alarm_delete(unsigned index) {
    if (!chronvs_alarm_get(index)) return false;
    chronvs_alarm_t old = alarms[index];
    memset(&alarms[index], 0, sizeof(alarms[index]));
    if (!save()) { alarms[index] = old; return false; }
    pending[index] = false;
    snooze[index] = 0;
    last_day[index] = -1;
    return true;
}
int chronvs_Relogio_alert(void) {
    if (timer_pending) return -1;
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) if (pending[i]) return i;
    for (unsigned i = 0; i < CHRONVS_REMINDER_LIMIT; ++i)
        if (reminder_pending[i]) return CHRONVS_ALARM_LIMIT + i;
    return -2;
}
void chronvs_Relogio_dismiss(uint32_t extra_minutes) {
    int alert = chronvs_Relogio_alert();
    if (alert == -1) {
        timer_pending = false;
        if (extra_minutes) chronvs_timer_start(extra_minutes);
    } else if (alert >= CHRONVS_ALARM_LIMIT) {
        chronvs_reminder_complete(alert - CHRONVS_ALARM_LIMIT);
    } else if (alert >= 0) {
        pending[alert] = false;
        if (extra_minutes) snooze[alert] = esp_timer_get_time() + (int64_t)extra_minutes * 60000000;
    }
}

bool chronvs_Relogio_time(chronvs_time_t *time) {
    if (!time || !clock_valid) return false;
    int64_t seconds = clock_seconds();
    if (seconds < 0 || seconds >= (int64_t)36525 * 86400) return false;
    int days = seconds / 86400, year = 2000, month = 1;
    while (days >= chronvs_calendar_days(year, month)) {
        days -= chronvs_calendar_days(year, month);
        if (++month == 13) { month = 1; ++year; }
    }
    *time = (chronvs_time_t){.year=year-2000, .month=month, .day=days+1,
        .hour=seconds%86400/3600, .minute=seconds%3600/60, .second=seconds%60,
        .weekday=(seconds/86400+6)%7, .valid=true};
    return true;
}
const chronvs_reminder_t *chronvs_reminder_get(unsigned index) {
    return index < CHRONVS_REMINDER_LIMIT && reminders[index].title[0] ? &reminders[index] : NULL;
}
static bool save_reminder(unsigned index, const chronvs_reminder_t *value) {
    chronvs_reminder_t old = reminders[index];
    reminders[index] = *value;
    if (!storage || nvs_set_blob(storage, "reminders_v1", reminders, sizeof(reminders)) != ESP_OK ||
        nvs_commit(storage) != ESP_OK) {
        reminders[index] = old;
        return false;
    }
    reminder_pending[index] = false;
    ++reminder_revision;
    return true;
}
uint32_t chronvs_reminder_revision(void) { return reminder_revision; }
bool chronvs_reminder_create(const chronvs_reminder_t *r) {
    if (!reminder_valid(r) || r->done || !clock_valid || reminder_seconds(r) <= clock_seconds()) return false;
    for (unsigned i = 0; i < CHRONVS_REMINDER_LIMIT; ++i)
        if (!reminders[i].title[0]) return save_reminder(i, r);
    return false;
}
bool chronvs_reminder_delete(unsigned index) {
    if (!chronvs_reminder_get(index)) return false;
    const chronvs_reminder_t empty = {0};
    return save_reminder(index, &empty);
}
bool chronvs_reminder_complete(unsigned index) {
    const chronvs_reminder_t *r = chronvs_reminder_get(index);
    if (!r) return false;
    chronvs_reminder_t completed = *r;
    completed.done = 1;
    return save_reminder(index, &completed);
}
