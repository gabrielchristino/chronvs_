#pragma once

#include "lvgl.h"

#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
/* Retained by the panel; callbacks run only in the LVGL UI task. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *owner;
    int16_t mask_id;
    lv_draw_mask_radius_param_t mask;
} chronvs_circle_clip_t;

typedef struct {
    chronvs_circle_clip_t *clip;
    bool background_only;
} chronvs_circle_clip_node_t;

void chronvs_circle_clip_init(chronvs_circle_clip_t *clip, lv_obj_t *root);
/* A subtree must paint within its object's bounds plus ext_draw_size. For a
 * transparent row, background_only clips its own pressed surface, while its
 * badge and label are bound separately. No escaping children or scrollbars. */
void chronvs_circle_clip_bind(chronvs_circle_clip_t *clip, lv_obj_t *object,
                             chronvs_circle_clip_node_t *node, bool background_only);
#endif
