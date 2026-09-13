#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initializes the shared buses, display, touch and LVGL port. */
void chronvs_board_init(void);

/* Sleeps with RAM retained until touch, power-button input or timeout. */
bool chronvs_board_light_sleep(uint32_t timeout_ms);
