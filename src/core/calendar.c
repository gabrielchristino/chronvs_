#include "core/calendar.h"

static bool leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int chronvs_calendar_days(int year, int month) {
    static const unsigned char days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < CHRONVS_CALENDAR_MIN_YEAR || year > CHRONVS_CALENDAR_MAX_YEAR ||
        month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && leap(year));
}

bool chronvs_calendar_valid(int year, int month, int day) {
    return day >= 1 && day <= chronvs_calendar_days(year, month);
}

int chronvs_calendar_ordinal(int year, int month, int day) {
    int total = day - 1;
    for (int y = CHRONVS_CALENDAR_MIN_YEAR; y < year; ++y) total += leap(y) ? 366 : 365;
    for (int m = 1; m < month; ++m) total += chronvs_calendar_days(year, m);
    return total;
}

int chronvs_calendar_weekday(int year, int month, int day) {
    return (chronvs_calendar_ordinal(year, month, day) + 6) % 7;
}

bool chronvs_calendar_step(int *year, int *month, int direction) {
    if (!year || !month || !chronvs_calendar_valid(*year, *month, 1) ||
        (direction != -1 && direction != 1)) return false;
    int index = (*year - CHRONVS_CALENDAR_MIN_YEAR) * 12 + *month - 1 + direction;
    if (index < 0 || index >= 1200) return false;
    *year = CHRONVS_CALENDAR_MIN_YEAR + index / 12;
    *month = index % 12 + 1;
    return true;
}
