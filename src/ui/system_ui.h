#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Adds touch gestures, brightness settings and display inactivity control. */
void chronvs_system_ui_init(lv_obj_t *touch_surface,
                            lv_timer_t *animation_timer);

/* True while the backlight is off and the clock should not be rendered. */
bool chronvs_system_ui_display_is_off(void);

/* Lets apps reset the inactivity timer and wake a dimmed display. */
void chronvs_system_ui_notify_activity(void);

/* Updates the battery percentage shown in the quick-settings panel. */
void chronvs_system_ui_set_battery(uint8_t percent, float voltage);

#ifdef CHRONVS_PANEL_AUTOTEST
/* Read-only state for the isolated scripted-gesture test. */
typedef struct { int16_t y; bool hidden, eco; uint8_t brightness, profile; } chronvs_ui_panel_probe_t;
chronvs_ui_panel_probe_t chronvs_system_ui_panel_probe(void);
#endif
