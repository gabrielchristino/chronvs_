#include "ui/arc_clip.h"

#ifdef CHRONVS_ARC_INNER_CLIP
#include <math.h>
#include <stdlib.h>
#include "src/draw/sw/lv_draw_sw.h"

/* The software arc masks its entire inner disk to transparent. On a partial
 * display strip, omit a conservative rectangular section of that disk before
 * invoking the unchanged renderer. End caps and AA stay outside this section. */
static void draw_arc(lv_draw_ctx_t *ctx, const lv_draw_arc_dsc_t *dsc,
                     const lv_point_t *center, uint16_t radius,
                     uint16_t start_angle, uint16_t end_angle) {
    const lv_area_t *original = ctx->clip_area;
    const int inner = (int)radius - dsc->width - 3;
    const int dy = LV_MAX(abs(original->y1 - center->y), abs(original->y2 - center->y)) + 1;
    if (radius < 64 || inner <= dy) {
        lv_draw_sw_arc(ctx, dsc, center, radius, start_angle, end_angle);
        return;
    }
    const int half = (int)sqrtf((float)(inner * inner - dy * dy)) - 1;
    if (half < 2) {
        lv_draw_sw_arc(ctx, dsc, center, radius, start_angle, end_angle);
        return;
    }
    lv_area_t side = *original;
    side.x2 = LV_MIN(side.x2, center->x - half);
    if (side.x1 <= side.x2) {
        ctx->clip_area = &side;
        lv_draw_sw_arc(ctx, dsc, center, radius, start_angle, end_angle);
    }
    side = *original;
    side.x1 = LV_MAX(side.x1, center->x + half);
    if (side.x1 <= side.x2) {
        ctx->clip_area = &side;
        lv_draw_sw_arc(ctx, dsc, center, radius, start_angle, end_angle);
    }
    ctx->clip_area = original;
}

static void arc_event(lv_event_t *event) {
    if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_DRAW_MAIN_BEGIN && code != LV_EVENT_DRAW_MAIN_END) return;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    if (code == LV_EVENT_DRAW_MAIN_BEGIN && ctx->draw_arc == lv_draw_sw_arc) ctx->draw_arc = draw_arc;
    else if (code == LV_EVENT_DRAW_MAIN_END && ctx->draw_arc == draw_arc) ctx->draw_arc = lv_draw_sw_arc;
}

void chronvs_ui_arc_clip_bind(lv_obj_t *arc) {
    lv_obj_add_event_cb(arc, arc_event, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(arc, arc_event, LV_EVENT_DRAW_MAIN_END, NULL);
}
#endif
