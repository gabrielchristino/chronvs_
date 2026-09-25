/* Test cache reuse and invalidation using the real watch implementation. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
static unsigned trig_calls;
static float counted_sinf(float angle) { ++trig_calls; return sinf(angle); }
static float counted_cosf(float angle) { ++trig_calls; return cosf(angle); }
#define sinf counted_sinf
#define cosf counted_cosf
#include "../src/apps/watch_app.c"
#undef sinf
#undef cosf

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
    return 0;
}
