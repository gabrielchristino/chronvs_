#pragma once
#include "lvgl.h"

typedef struct {
    lv_point_t start;
    bool consumed;
    void (*back)(void);
} chronvs_ui_app_input_t;

/* Bind once to each newly created subtree, after its local event handlers.
 * Reports real contact activity and consumes a right swipe before release.
 * The state must live as long as the app; back may delete the current page. */
void chronvs_ui_app_input_bind(lv_obj_t *tree, chronvs_ui_app_input_t *state);
