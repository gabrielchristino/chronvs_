/* Count date conversions while running the actual service and regression suite. */
#define main service_regression_main
#include "Relogio_service_test.c"
#undef main
#include "core/calendar.h"

static unsigned conversions;
static int counted_ordinal(int year, int month, int day) {
    ++conversions;
    return chronvs_calendar_ordinal(year, month, day);
}
#define chronvs_calendar_ordinal counted_ordinal
#include "../src/services/Relogio_service.c"
#undef chronvs_calendar_ordinal

int main(void) {
    assert(service_regression_main() == 0);
    memset(persisted, 0, sizeof(persisted));
    memset(persisted_reminders, 0, sizeof(persisted_reminders));
    now_us = 0; chronvs_Relogio_init();
    set_time(26, 9, 12, 7, 59, 0);
    chronvs_reminder_t r = {.year=26,.month=9,.day=12,.hour=8,.title="Agenda"};
    for (unsigned i=0; i<CHRONVS_REMINDER_LIMIT; ++i) assert(chronvs_reminder_create(&r));
    conversions = 0;
    for (unsigned i=0; i<100; ++i) {
        chronvs_Relogio_poll(); now_us += 10000;
    }
    assert(conversions == CHRONVS_REMINDER_LIMIT);
    chronvs_Relogio_poll();
    assert(conversions == 2 * CHRONVS_REMINDER_LIMIT);
    /* Editing within the same second invalidates the civil scan. */
    assert(chronvs_reminder_delete(0)); conversions = 0;
    chronvs_Relogio_poll(); assert(conversions == CHRONVS_REMINDER_LIMIT - 1);
    assert(chronvs_reminder_create(&r)); conversions = 0;
    chronvs_Relogio_poll(); assert(conversions == CHRONVS_REMINDER_LIMIT);
    /* Timer starts halfway into a civil second and must expire there too. */
    now_us += 500000; chronvs_timer_start(1);
    now_us += 59500000; chronvs_Relogio_poll();
    assert(chronvs_timer_running());
    now_us += 500000; chronvs_Relogio_poll();
    assert(!chronvs_timer_running() && chronvs_Relogio_alert() == -1);
    /* Corrections are observed immediately, including overdue reminders. */
    chronvs_Relogio_init();
    set_time(26,9,12,7,58,0);
    assert(chronvs_Relogio_alert() == -2);
    set_time(26,9,12,8,0,0);
    assert(chronvs_Relogio_alert() == CHRONVS_ALARM_LIMIT);
    for (unsigned i=0; i<CHRONVS_REMINDER_LIMIT; ++i) assert(chronvs_reminder_delete(i));
    set_time(26,9,12,7,59,59);
    assert(chronvs_alarm_create(8,0,127)); advance(1);
    assert(chronvs_Relogio_alert() == 0);
    now_us += 500000; chronvs_Relogio_dismiss(1);
    now_us += 59500000; chronvs_Relogio_poll();
    assert(chronvs_Relogio_alert() == -2);
    now_us += 500000; chronvs_Relogio_poll();
    assert(chronvs_Relogio_alert() == 0); /* Snooze also bypasses the civil guard. */
    printf("Agenda: %u conversions per second for 100 polls; timer retains subsecond deadline.\n",
           CHRONVS_REMINDER_LIMIT);
    return 0;
}
