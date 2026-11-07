#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initializes the shared buses, display, touch and LVGL port. */
void chronvs_board_init(void);

/* One request per physical hold; consumed only by the UI task. */
bool chronvs_board_take_power_menu_request(void);
/* Execute only after explicit confirmation in the power menu. */
void chronvs_board_power_off(void);
void chronvs_board_restart(void);

/* Sleeps with RAM retained until touch, power-button input or timeout. */
bool chronvs_board_light_sleep(uint32_t timeout_ms);
