#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour, day, weekday, month, year;
    bool valid;
} chronvs_time_t;

chronvs_time_t chronvs_rtc_read(void);
/* Main/UI task only. Samples at boot/wake, every 60 s with a trusted clock,
 * or every second until a reference is available. No I2C with display off. */
void chronvs_rtc_refresh(bool display_off);
