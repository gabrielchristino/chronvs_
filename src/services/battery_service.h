#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float voltage;
    uint8_t percent;
    bool valid;
} chronvs_battery_status_t;

void chronvs_battery_init(void);
chronvs_battery_status_t chronvs_battery_read(void);
