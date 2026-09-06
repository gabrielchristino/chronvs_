#include "ui/aion_widgets.h"

void chronvs_aion_surface(lv_obj_t *obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 412, 412);
    lv_obj_set_style_bg_color(obj, lv_color_hex(CHRONVS_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(CHRONVS_UI_TEXT), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
}
lv_obj_t *chronvs_aion_label(lv_obj_t *parent, const char *text, int y, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}
static lv_obj_t *create_button(lv_obj_t *parent, const char *text, int x, int y,
                               int width, int height, lv_event_cb_t callback, intptr_t value,
                               bool outline) {
    lv_obj_t *button = lv_btn_create(parent);
    chronvs_ui_style_control(button, outline);
    lv_obj_set_size(button, width, height);
    lv_obj_align(button, LV_ALIGN_TOP_MID, x, y);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)value);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

lv_obj_t *chronvs_aion_button(lv_obj_t *parent, const char *text, int x, int y,
                             int width, int height, lv_event_cb_t callback, intptr_t value) {
    return create_button(parent, text, x, y, width, height, callback, value, false);
}

lv_obj_t *chronvs_aion_circle(lv_obj_t *parent, const char *text, unsigned index,
                             int top, lv_event_cb_t callback, intptr_t value) {
    if (index >= 7) return NULL;
    return chronvs_aion_button(parent, text, chronvs_ui_hex_offsets[index].x,
                               top + chronvs_ui_hex_offsets[index].y,
                               CHRONVS_UI_CIRCLE_SIZE, CHRONVS_UI_CIRCLE_SIZE, callback, value);
}
lv_obj_t *chronvs_aion_action(lv_obj_t *parent, const char *text, int x, int y,
                             int width, bool outline, lv_event_cb_t callback, intptr_t value) {
    return create_button(parent, text, x, y, width, CHRONVS_UI_ACTION_HEIGHT,
                         callback, value, outline);
}
