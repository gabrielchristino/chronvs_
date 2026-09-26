/* Compare circle-cache configurations using real LVGL and partial buffers. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "lvgl.h"
#include "src/misc/lv_gc.h"
#include "../src/apps/watch_app.c"

int64_t esp_timer_get_time(void) { return 0; }
bool chronvs_system_ui_display_is_off(void) { return false; }
void chronvs_system_ui_init(lv_obj_t *surface, lv_timer_t *timer) {
    (void)surface; (void)timer;
}
bool chronvs_Relogio_time(chronvs_time_t *time) { (void)time; return false; }
void chronvs_apps_add(const chronvs_app_t *app) { (void)app; }

static unsigned misses, large_misses;
static uint32_t checksum = 2166136261u;
static size_t peak_used;
static size_t smallest_block = LV_MEM_SIZE;
static FILE *pixel_stream;
void __real_lv_draw_mask_radius_init(lv_draw_mask_radius_param_t *, const lv_area_t *, lv_coord_t, bool);
void __wrap_lv_draw_mask_radius_init(lv_draw_mask_radius_param_t *param,
                                   const lv_area_t *area, lv_coord_t radius, bool inv) {
    int actual = LV_MAX(0, LV_MIN(radius, LV_MIN(lv_area_get_width(area), lv_area_get_height(area)) / 2));
    bool hit = actual == 0;
    for (unsigned i = 0; i < LV_CIRCLE_CACHE_SIZE; ++i)
        if (LV_GC_ROOT(_lv_circle_cache[i]).radius == actual) hit = true;
    if (!hit) { ++misses; if (actual >= 180) ++large_misses; }
    __real_lv_draw_mask_radius_init(param, area, radius, inv);
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    peak_used = LV_MAX(peak_used, memory.total_size - memory.free_size);
    smallest_block = LV_MIN(smallest_block, memory.free_biggest_size);
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    if (pixel_stream)
        assert(fwrite(colors, sizeof(*colors), lv_area_get_size(area), pixel_stream) == lv_area_get_size(area));
    for (unsigned i = 0; i < lv_area_get_size(area); ++i) {
        checksum ^= colors[i].full;
        checksum *= 16777619u;
    }
    lv_disp_flush_ready(driver);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    lv_init();
    static lv_color_t pixels[412 * 412 / 20];
    static lv_disp_draw_buf_t draw;
    static lv_disp_drv_t driver;
    lv_disp_draw_buf_init(&draw, pixels, NULL, 412 * 412 / 20);
    lv_disp_drv_init(&driver);
    driver.hor_res = driver.ver_res = 412;
    driver.draw_buf = &draw;
    driver.flush_cb = flush;
    lv_disp_t *display = lv_disp_drv_register(&driver);
    create_watch_app(lv_scr_act());
    lv_refr_now(display);
    pixel_stream = fopen(argv[1], "wb");
    assert(pixel_stream);
    misses = large_misses = 0;
    checksum = 2166136261u;
    /* Every minute angle, date and weekday; moved/partly clipped watch too. */
    for (unsigned i = 0; i < 60; ++i) {
        chronvs_time_t time = {.valid=true, .year=26, .month=9,
            .day=1+i%31, .weekday=i%7, .hour=i%24, .minute=i, .second=i};
        chronvs_watch_app_set_time(&time);
        lv_obj_set_y(clock_face, i < 30 ? 0 : -(int)(i - 29) * 5);
        lv_obj_invalidate(clock_face);
        lv_refr_now(display);
    }
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.free_biggest_size > 16384);
    assert(fclose(pixel_stream) == 0);
    printf("cache=%u checksum=%08x misses=%u large_misses=%u used=%u biggest=%u peak_sampled=%u smallest_sampled=%u\n",
           LV_CIRCLE_CACHE_SIZE, checksum, misses, large_misses,
           (unsigned)(memory.total_size-memory.free_size), (unsigned)memory.free_biggest_size,
           (unsigned)peak_used, (unsigned)smallest_block);
    return 0;
}
