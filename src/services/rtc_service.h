#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour, day, weekday, month, year;
    bool valid;
} chronvs_time_t;

chronvs_time_t chronvs_rtc_read(void);
