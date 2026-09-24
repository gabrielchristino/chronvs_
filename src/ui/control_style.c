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

/* Immutable after initialization: each button keeps only style references.
 * Local setters in apps still override these shared defaults. */
static lv_style_t control_base[2], control_pressed, control_checked;
static lv_style_t control_checked_pressed, control_disabled;
static bool controls_initialized;

static void init_controls(void) {
    if (controls_initialized) return;
    for (unsigned outline = 0; outline < 2; ++outline) {
        lv_style_t *style = &control_base[outline];
        lv_style_init(style);
        lv_style_set_radius(style, LV_RADIUS_CIRCLE);
        lv_style_set_bg_opa(style, LV_OPA_COVER);
        lv_style_set_bg_color(style, lv_color_hex(outline ? CHRONVS_UI_PANEL : CHRONVS_UI_SURFACE));
        lv_style_set_border_width(style, 1);
        lv_style_set_border_color(style, lv_color_hex(outline ? CHRONVS_UI_SURFACE : CHRONVS_UI_TEXT_DIM));
        lv_style_set_text_color(style, lv_color_hex(CHRONVS_UI_TEXT));
        lv_style_set_text_font(style, &lv_font_montserrat_18);
        lv_style_set_pad_all(style, 0);
    }
    lv_style_init(&control_pressed);
    lv_style_set_bg_color(&control_pressed, lv_color_hex(0x5E6C5D));
    lv_style_init(&control_checked);
    lv_style_set_bg_color(&control_checked, lv_color_hex(CHRONVS_UI_ACCENT));
    lv_style_set_border_color(&control_checked, lv_color_hex(CHRONVS_UI_ACCENT));
    lv_style_set_text_color(&control_checked, lv_color_hex(CHRONVS_UI_PANEL));
    lv_style_init(&control_checked_pressed);
    lv_style_set_bg_color(&control_checked_pressed, lv_color_hex(0xD9A442));
    lv_style_init(&control_disabled);
    lv_style_set_bg_color(&control_disabled, lv_color_hex(CHRONVS_UI_PANEL));
    lv_style_set_border_color(&control_disabled, lv_color_hex(CHRONVS_UI_SURFACE));
    lv_style_set_text_color(&control_disabled, lv_color_hex(CHRONVS_UI_TEXT_DIM));
    controls_initialized = true;
}

void chronvs_ui_style_control(lv_obj_t *button, bool outline) {
    init_controls();
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &control_base[outline ? 1 : 0], 0);
    lv_obj_add_style(button, &control_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(button, &control_checked, LV_STATE_CHECKED);
    lv_obj_add_style(button, &control_checked_pressed, LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_add_style(button, &control_disabled, LV_STATE_DISABLED);
}
