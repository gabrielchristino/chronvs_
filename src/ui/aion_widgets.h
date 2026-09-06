#pragma once
#include "lvgl.h"
#include "ui/control_style.h"

lv_obj_t *chronvs_aion_label(lv_obj_t *parent, const char *text, int y, const lv_font_t *font);
lv_obj_t *chronvs_aion_button(lv_obj_t *parent, const char *text, int x, int y,
                             int width, int height, lv_event_cb_t callback, intptr_t value);
void chronvs_aion_surface(lv_obj_t *obj);
lv_obj_t *chronvs_aion_circle(lv_obj_t *parent, const char *text, unsigned index,
                             int top, lv_event_cb_t callback, intptr_t value);
lv_obj_t *chronvs_aion_action(lv_obj_t *parent, const char *text, int x, int y,
                             int width, bool outline, lv_event_cb_t callback, intptr_t value);
