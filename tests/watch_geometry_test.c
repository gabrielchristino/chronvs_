/* Test cache reuse and invalidation using the real watch implementation. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "lvgl.h"
static unsigned trig_calls;
static unsigned label_descriptors, line_calls;
static void counted_label_init(lv_draw_label_dsc_t *dsc) {
    ++label_descriptors;
    lv_draw_label_dsc_init(dsc);
}
static void count_line(lv_draw_ctx_t *ctx, const lv_draw_line_dsc_t *dsc,
                       const lv_point_t *start, const lv_point_t *end) {
    (void)ctx; (void)dsc; (void)start; (void)end; ++line_calls;
}
static float counted_sinf(float angle) { ++trig_calls; return sinf(angle); }
static float counted_cosf(float angle) { ++trig_calls; return cosf(angle); }
#define sinf counted_sinf
#define cosf counted_cosf
#define lv_draw_label_dsc_init counted_label_init
#include "../src/apps/watch_app.c"
#undef sinf
#undef cosf
#undef lv_draw_label_dsc_init

int64_t esp_timer_get_time(void) { return 0; }
bool chronvs_system_ui_display_is_off(void) { return false; }
void chronvs_system_ui_init(lv_obj_t *surface, lv_timer_t *timer) {
    (void)surface; (void)timer;
}
bool chronvs_Relogio_time(chronvs_time_t *time) { (void)time; return false; }
void chronvs_apps_add(const chronvs_app_t *app) { (void)app; }

int main(void) {
    update_chapter_geometry(205.5f, 205.5f, 1);
    assert(trig_calls > 0);
    trig_calls = 0;
    for (unsigned i = 0; i < 100; ++i)
        update_chapter_geometry(205.5f, 205.5f, 1);
    assert(trig_calls == 0);
    const lv_point_t minute_zero = chapter.minutes[0][0];
    for (unsigned day = 1; day <= 31; ++day) {
        update_chapter_geometry(205.5f, 205.5f, day);
        /* Today's date stays at six o'clock, while the minute chapter is fixed. */
        /* The center lies between pixels; either adjacent x is valid after rounding. */
        assert(fabsf(chapter.dates[day - 1].x - 205.5f) <= 0.5f);
        assert(chapter.dates[day - 1].y == 401);
        assert(chapter.minutes[0][0].x == minute_zero.x);
        assert(chapter.minutes[0][0].y == minute_zero.y);
    }
    assert(trig_calls == 30 * 31 * 2);
    update_chapter_geometry(205.5f, 105.5f, 31);
    assert(chapter.dates[30].y == 301);
    assert(fabsf(chapter.minutes[0][0].y - (105.5f - 168)) <= 0.5f);
    update_chapter_geometry(105.5f, 105.5f, 31);
    assert(fabsf(chapter.dates[30].x - 105.5f) <= 0.5f);
    assert(chapter.minutes[0][0].x == minute_zero.x - 100);
    trig_calls = 0;
    update_chapter_geometry(105.5f, 105.5f, 31);
    assert(trig_calls == 0);
    printf("Watch geometry: reuse, all dates and movement passed; cache=%u bytes.\n",
           (unsigned)sizeof(chapter));
    lv_area_t clip = {.x1=0, .y1=20, .x2=411, .y2=39};
    lv_draw_ctx_t ctx = {.clip_area=&clip, .draw_line=count_line};
    draw_line(&ctx, (lv_point_t){10,100}, (lv_point_t){30,100}, COLOR_INK, 2, true);
    draw_line(&ctx, (lv_point_t){-30,25}, (lv_point_t){-20,35}, COLOR_INK, 2, true);
    assert(line_calls == 0);
    /* Keep caps near the edge and lines crossing the strip with both ends outside. */
    draw_line(&ctx, (lv_point_t){10,19}, (lv_point_t){30,19}, COLOR_INK, 2, true);
    draw_line(&ctx, (lv_point_t){10,-10}, (lv_point_t){30,80}, COLOR_INK, 2, true);
    assert(line_calls == 2);
    draw_text(&ctx, 100, 150, "31", &lv_font_montserrat_12, COLOR_INK, 24);
    assert(label_descriptors == 0);
    /* No draw_letter backend is needed to verify the visible descriptor path. */
    draw_text(&ctx, 100, 25, "31", &lv_font_montserrat_12, COLOR_INK, 24);
    assert(label_descriptors == 1);
    puts("Watch clipping: off-strip work skipped, edge and crossing lines retained.");
    return 0;
}
