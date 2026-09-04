#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Adds touch gestures, brightness settings and display inactivity control. */
void chronvs_watch_controls_init(lv_obj_t *touch_surface,
                                 lv_timer_t *animation_timer);

/* True while the backlight is off and the clock should not be rendered. */
bool chronvs_watch_display_is_off(void);

/* Updates the battery percentage shown in the quick-settings panel. */
void chronvs_watch_set_battery(uint8_t percent, float voltage);
