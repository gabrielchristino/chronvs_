#pragma once

#include <stdbool.h>

#define CHRONVS_CALENDAR_MIN_YEAR 2000
#define CHRONVS_CALENDAR_MAX_YEAR 2099

int chronvs_calendar_days(int year, int month);
bool chronvs_calendar_valid(int year, int month, int day);
/* Days since 2000-01-01; inputs must be valid civil dates. */
int chronvs_calendar_ordinal(int year, int month, int day);
/* Sunday = 0. Does not depend on the RTC weekday register or timezone. */
int chronvs_calendar_weekday(int year, int month, int day);
bool chronvs_calendar_step(int *year, int *month, int direction);
