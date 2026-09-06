#include "ui/control_style.h"

const lv_point_t chronvs_ui_hex_offsets[7] = {
    {-43, 0}, {43, 0}, {-86, 72}, {0, 72}, {86, 72}, {-43, 144}, {43, 144},
};

void chronvs_ui_style_arc(lv_obj_t *arc) {
    lv_obj_set_style_arc_color(arc, lv_color_hex(CHRONVS_UI_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(CHRONVS_UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc, lv_color_hex(CHRONVS_UI_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 7, LV_PART_KNOB);
}

void chronvs_ui_style_control(lv_obj_t *button, bool outline) {
    lv_obj_remove_style_all(button);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(outline ? CHRONVS_UI_PANEL : CHRONVS_UI_SURFACE), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(outline ? CHRONVS_UI_SURFACE : CHRONVS_UI_TEXT_DIM), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(CHRONVS_UI_TEXT), 0);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_18, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x5E6C5D), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(CHRONVS_UI_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(button, lv_color_hex(CHRONVS_UI_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button, lv_color_hex(CHRONVS_UI_PANEL), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xD9A442), LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(CHRONVS_UI_PANEL), LV_STATE_DISABLED);
    lv_obj_set_style_border_color(button, lv_color_hex(CHRONVS_UI_SURFACE), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(CHRONVS_UI_TEXT_DIM), LV_STATE_DISABLED);
}
