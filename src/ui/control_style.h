#pragma once
#include "lvgl.h"

#define CHRONVS_UI_PANEL 0x26302B
#define CHRONVS_UI_SURFACE 0x748173
#define CHRONVS_UI_TEXT 0xF2F2E9
#define CHRONVS_UI_TEXT_DIM 0xB7C0B5
#define CHRONVS_UI_ACCENT 0xF2B84B
#define CHRONVS_UI_CIRCLE_SIZE 70
#define CHRONVS_UI_ACTION_HEIGHT 54
#define CHRONVS_UI_ACTION_WIDTH 140
#define CHRONVS_UI_PAIR_WIDTH 120

extern const lv_point_t chronvs_ui_hex_offsets[7];
/* Apply to a new control BEFORE setting geometry; removes the theme defaults. */
void chronvs_ui_style_control(lv_obj_t *button, bool outline);
void chronvs_ui_style_arc(lv_obj_t *arc);
