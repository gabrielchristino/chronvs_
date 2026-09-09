#include "ui/app_input.h"
#include "ui/system_ui.h"
#include <stdlib.h>

static void contact_event(lv_event_t *event) {
    /* Some widgets bubble events. Handle each sample only at its target. */
    if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED) return;
    lv_indev_t *input = lv_indev_get_act();
    if (!input) return;
    chronvs_system_ui_notify_activity();
    chronvs_ui_app_input_t *state = lv_event_get_user_data(event);
    lv_point_t point; lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        state->start = point;
        state->consumed = false;
    } else if (code == LV_EVENT_PRESSING && !state->consumed) {
        int dx = point.x - state->start.x;
        int dy = abs(point.y - state->start.y);
        if (state->back && dx > 80 && dx > dy + 20) {
            state->consumed = true;
            lv_indev_wait_release(input);
            state->back();
        }
    }
}

void chronvs_ui_app_input_bind(lv_obj_t *tree, chronvs_ui_app_input_t *state) {
    lv_obj_add_event_cb(tree, contact_event, LV_EVENT_ALL, state);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(tree); ++i)
        chronvs_ui_app_input_bind(lv_obj_get_child(tree, i), state);
}
