#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "services/aion_service.h"

static int64_t now_us;
static unsigned char persisted[CHRONVS_ALARM_LIMIT * sizeof(chronvs_alarm_t)];
static unsigned char persisted_reminders[CHRONVS_REMINDER_LIMIT * sizeof(chronvs_reminder_t)];
static bool fail_save;
int64_t esp_timer_get_time(void) { return now_us; }
int nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 1; return 0; }
int nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *size) {
    (void)h;
    if (!strcmp(k, "reminders_v1")) {
        assert(*size == sizeof(persisted_reminders)); memcpy(out, persisted_reminders, *size); return 0;
    }
    if (strcmp(k, "alarms_v1")) return -1;
    assert(*size == sizeof(persisted)); memcpy(out, persisted, *size); return 0;
}
int nvs_set_blob(nvs_handle_t h, const char *k, const void *in, size_t size) {
    (void)h;
    if (!strcmp(k, "reminders_v1")) {
        assert(size == sizeof(persisted_reminders));
        if (fail_save) return -1;
        memcpy(persisted_reminders, in, size); return 0;
    }
    assert(!strcmp(k, "alarms_v1") && size == sizeof(persisted));
    if (fail_save) return -1;
    memcpy(persisted, in, size); return 0;
}
int nvs_commit(nvs_handle_t h) { (void)h; return 0; }
static void advance(int seconds) { now_us += (int64_t)seconds * 1000000; chronvs_aion_poll(); }
static void set_time(int year, int month, int day, int hour, int minute, int second) {
    chronvs_time_t t = {.year=year,.month=month,.day=day,.hour=hour,.minute=minute,.second=second,.valid=true};
    chronvs_aion_observe_time(&t);
    chronvs_aion_poll();
}
static void clear_alarms(void) {
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i)
        if (chronvs_alarm_get(i)) assert(chronvs_alarm_delete(i));
    chronvs_timer_cancel();
}
int main(void) {
    chronvs_aion_init();
    assert(!chronvs_alarm_create(24, 0, 1));
    assert(!chronvs_alarm_create(1, 60, 1));
    assert(!chronvs_alarm_create(1, 0, 0));
    assert(!chronvs_alarm_create(1, 0, 128));
    chronvs_timer_start(5);
    advance(299);
    assert(chronvs_timer_remaining() == 1 && chronvs_aion_alert() == -2);
    set_time(26,9,6,23,59,59); /* wall clock correction cannot change the timer */
    advance(1);
    assert(chronvs_aion_alert() == -1 && !chronvs_timer_running());
    const unsigned extras[] = {1,5,10,15,30,60,120};
    for (unsigned i = 0; i < 7; ++i) {
        chronvs_aion_dismiss(extras[i]);
        assert(chronvs_timer_remaining() == extras[i] * 60);
        advance(extras[i] * 60);
        assert(chronvs_aion_alert() == -1);
    }
    chronvs_aion_dismiss(0);
    assert(chronvs_aion_alert() == -2);
    chronvs_timer_start(10); chronvs_timer_cancel(); advance(600);
    assert(chronvs_aion_alert() == -2);

    /* Every weekday, overnight recurrence and multiple selected days. */
    set_time(26,9,6,23,59,59); /* Sunday */
    for (unsigned i = 0; i < 7; ++i) assert(chronvs_alarm_create(0,0,1u << i));
    for (unsigned d = 1; d <= 14; ++d) {
        advance(d == 1 ? 1 : 86400);
        assert(chronvs_aion_alert() == (int)(d % 7));
        chronvs_aion_dismiss(0); chronvs_aion_poll();
        assert(chronvs_aion_alert() == -2); /* no retrigger in the same minute */
    }
    clear_alarms();
    set_time(26,9,6,7,59,0);
    assert(chronvs_alarm_create(8,0,127));
    assert(chronvs_alarm_create(8,0,1));
    chronvs_timer_start(1);
    advance(60);
    assert(chronvs_aion_alert() == -1);
    chronvs_aion_dismiss(0);
    assert(chronvs_aion_alert() == 0);
    chronvs_aion_dismiss(5);
    assert(chronvs_aion_alert() == 1);
    chronvs_aion_dismiss(0);
    advance(299); assert(chronvs_aion_alert() == -2);
    advance(1); assert(chronvs_aion_alert() == 0);
    chronvs_aion_dismiss(5);
    assert(chronvs_alarm_delete(0)); advance(300);
    assert(chronvs_aion_alert() == -2); /* deleting cancels snooze */
    clear_alarms();

    /* Leap day and year rollover without further RTC observations. */
    set_time(24,2,28,23,59,59);
    assert(chronvs_alarm_create(0,0,1 << 4)); /* Thursday Feb 29 */
    advance(1); assert(chronvs_aion_alert() == 0);
    clear_alarms();
    set_time(26,12,31,23,59,59);
    assert(chronvs_alarm_create(0,0,1 << 5)); /* Friday Jan 1 */
    advance(1); assert(chronvs_aion_alert() == 0);
    clear_alarms();

    fail_save = true;
    assert(!chronvs_alarm_create(8,0,127)); assert(!chronvs_alarm_get(0));
    fail_save = false;
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) assert(chronvs_alarm_create(i,30,127));
    assert(!chronvs_alarm_create(22,0,1));
    fail_save = true;
    assert(!chronvs_alarm_delete(0)); assert(chronvs_alarm_get(0));
    fail_save = false;
    chronvs_aion_init(); /* saved list is restored */
    for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
        const chronvs_alarm_t *a = chronvs_alarm_get(i);
        assert(a && a->hour == i && a->minute == 30 && a->days == 127);
    }
    puts("Aion: timer, extensions, weekdays, recurrence, snooze, queue, rollover and NVS tests passed.");
    clear_alarms();
    chronvs_reminder_t reminder = {.year=26,.month=9,.day=12,.hour=8,.title="Reunião"};
    assert(!chronvs_reminder_create(&reminder)); /* No trusted clock after boot. */
    set_time(26,9,12,7,59,0);
    fail_save=true; assert(!chronvs_reminder_create(&reminder)); assert(!chronvs_reminder_get(0));
    fail_save=false;
    chronvs_reminder_t bad=reminder; bad.month=2; bad.day=30; assert(!chronvs_reminder_create(&bad));
    bad=reminder; bad.title[0]=0; assert(!chronvs_reminder_create(&bad));
    bad=reminder; strcpy(bad.title,"   "); assert(!chronvs_reminder_create(&bad));
    bad=reminder; memset(bad.title,'a',sizeof(bad.title)); assert(!chronvs_reminder_create(&bad));
    bad=reminder; strcpy(bad.title,"a\nb"); assert(!chronvs_reminder_create(&bad));
    bad=reminder; bad.hour=7; assert(!chronvs_reminder_create(&bad));
    assert(chronvs_reminder_create(&reminder)); assert(chronvs_reminder_create(&reminder));
    assert(chronvs_alarm_create(8,0,127)); chronvs_timer_start(1);
    advance(60); assert(chronvs_aion_alert()==-1);
    chronvs_aion_dismiss(0); assert(chronvs_aion_alert()==0);
    chronvs_aion_dismiss(0); assert(chronvs_aion_alert()==CHRONVS_ALARM_LIMIT);
    fail_save=true; assert(!chronvs_reminder_complete(0)); assert(chronvs_aion_alert()==CHRONVS_ALARM_LIMIT);
    assert(!chronvs_reminder_delete(0)); assert(chronvs_reminder_get(0));
    fail_save=false; assert(chronvs_reminder_complete(0));
    assert(chronvs_aion_alert()==CHRONVS_ALARM_LIMIT+1);
    chronvs_aion_init(); set_time(26,9,13,12,0,0); /* Missed reminders survive reboot. */
    assert(chronvs_reminder_get(0)->done && chronvs_aion_alert()==CHRONVS_ALARM_LIMIT+1);
    assert(chronvs_reminder_delete(1)); assert(chronvs_aion_alert()==-2);
    set_time(26,9,12,7,59,0); advance(60); /* A backward correction cannot repeat completed reminders. */
    chronvs_aion_dismiss(0); assert(chronvs_aion_alert()==-2);
    assert(chronvs_reminder_delete(0)); clear_alarms();
    set_time(24,2,28,23,59,59);
    reminder=(chronvs_reminder_t){.year=24,.month=2,.day=29,.title="Bissexto"};
    assert(chronvs_reminder_create(&reminder)); advance(1); assert(chronvs_aion_alert()==CHRONVS_ALARM_LIMIT);
    assert(chronvs_reminder_delete(0));
    set_time(26,12,31,23,59,59);
    reminder=(chronvs_reminder_t){.year=27,.month=1,.day=1,.title="Ano novo"};
    for (unsigned i=0;i<CHRONVS_REMINDER_LIMIT;++i) assert(chronvs_reminder_create(&reminder));
    assert(!chronvs_reminder_create(&reminder)); advance(1);
    for (unsigned i=0;i<CHRONVS_REMINDER_LIMIT;++i) {
        assert(chronvs_aion_alert()==CHRONVS_ALARM_LIMIT+(int)i); assert(chronvs_reminder_complete(i));
    }
    chronvs_aion_init(); set_time(27,1,2,0,0,0); assert(chronvs_aion_alert()==-2);
    puts("Hemera reminders: validation, capacity, NVS, overdue/reboot, one-shot completion, queue and calendar boundaries passed.");
    return 0;
}
