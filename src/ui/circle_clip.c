#include "ui/circle_clip.h"

#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
#include <stdlib.h>

/* Conservative interior: the outer two pixels keep LVGL's original AA mask. */
static bool inside_circle(const lv_area_t *area, const lv_area_t *circle) {
    const int32_t cx = circle->x1 + circle->x2;
    const int32_t cy = circle->y1 + circle->y2;
    const int32_t radius = LV_MIN(lv_area_get_width(circle), lv_area_get_height(circle)) - 4;
    const int32_t dx = LV_MAX(abs(2 * area->x1 - cx), abs(2 * area->x2 - cx));
    const int32_t dy = LV_MAX(abs(2 * area->y1 - cy), abs(2 * area->y2 - cy));
    return radius > 0 && dx * dx + dy * dy < radius * radius;
}

static void cover_event(lv_event_t *event) {
    chronvs_circle_clip_t *clip = lv_event_get_user_data(event);
    lv_cover_check_info_t *info = lv_event_get_param(event);
    /* Prevent cover-search from starting at a child outside the circle and
     * bypassing the normal root traversal. Interior opaque strips can cover. */
    if (!inside_circle(info->area, &clip->root->coords)) info->res = LV_COVER_RES_MASKED;
}

static void release_mask(chronvs_circle_clip_t *clip) {
    lv_draw_mask_remove_id(clip->mask_id);
    lv_draw_mask_free_param(&clip->mask);
    clip->owner = NULL;
}

static void draw_event(lv_event_t *event) {
    chronvs_circle_clip_node_t *node = lv_event_get_user_data(event);
    chronvs_circle_clip_t *clip = node->clip;
    lv_obj_t *object = lv_event_get_current_target(event);
    if (lv_event_get_target(event) != object) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DRAW_MAIN_BEGIN && clip->owner == NULL) {
        if (node->background_only && lv_obj_get_style_bg_opa(object, 0) <= LV_OPA_MIN) return;
        lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
        lv_area_t bounds = object->coords, visible;
        const lv_coord_t extra = _lv_obj_get_ext_draw_size(object);
        lv_area_increase(&bounds, extra, extra);
        if (!_lv_area_intersect(&visible, &bounds, ctx->clip_area) ||
            inside_circle(&visible, &clip->root->coords)) return;
        lv_draw_mask_radius_init(&clip->mask, &clip->root->coords, LV_RADIUS_CIRCLE, false);
        clip->mask_id = lv_draw_mask_add(&clip->mask, clip);
        if (clip->mask_id >= 0) clip->owner = object;
        else lv_draw_mask_free_param(&clip->mask);
    } else if (clip->owner == object &&
               (code == LV_EVENT_DELETE || code == LV_EVENT_DRAW_POST_END ||
                (node->background_only && code == LV_EVENT_DRAW_MAIN_END))) {
        release_mask(clip);
    }
}

void chronvs_circle_clip_init(chronvs_circle_clip_t *clip, lv_obj_t *root) {
    *clip = (chronvs_circle_clip_t){.root = root, .mask_id = -1};
    lv_obj_set_style_clip_corner(root, false, 0);
    lv_obj_add_event_cb(root, cover_event, LV_EVENT_COVER_CHECK, clip);
}

void chronvs_circle_clip_bind(chronvs_circle_clip_t *clip, lv_obj_t *object,
                             chronvs_circle_clip_node_t *node, bool background_only) {
    *node = (chronvs_circle_clip_node_t){.clip = clip, .background_only = background_only};
    lv_obj_add_event_cb(object, draw_event, LV_EVENT_ALL, node);
}
#endif
