/* Compare circle-cache configurations using real LVGL and partial buffers. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "lvgl.h"
#include "src/misc/lv_gc.h"
#pragma GCC push_options
#ifdef CHRONVS_TEST_WATCH_BASELINE
#pragma GCC optimize ("Og")
#endif
#include "../src/apps/watch_app.c"
#pragma GCC pop_options

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
#if defined(CHRONVS_TEST_RING_CACHE) && !defined(CHRONVS_WATCH_RINGS_REFERENCE)
static void test_ring_fallbacks(void) {
    static lv_color_t pixels[412 * 20], reference[412 * 20];
    lv_draw_sw_ctx_t sw;
    lv_draw_sw_init_ctx(NULL, &sw.base_draw);
    lv_color_t *saved_line = ring_cache_line;
    assert(saved_line);
    circle_cover_count = 0;
    const lv_area_t mask_area = {5,5,406,406};
    for (int masked=0; masked<2; ++masked) {
        lv_draw_mask_radius_param_t mask;
        int16_t mask_id=-1;
        if (masked) {
            lv_draw_mask_radius_init(&mask,&mask_area,200,false);
            mask_id=lv_draw_mask_add(&mask,NULL); assert(mask_id>=0);
        }
        const int rows[]={0,190,392};
        for(unsigned i=0;i<3;++i) {
            lv_area_t area={0,rows[i],411,rows[i]+19};
            sw.base_draw.buf=pixels; sw.base_draw.buf_area=&area;
            sw.base_draw.clip_area=&area;
            for(int mode=0;mode<2;++mode) {
                ring_cache_line=mode ? saved_line : NULL;
                for(unsigned x=0;x<412*20;++x) pixels[x]=lv_color_hex(COLOR_BEZEL_DARK);
                draw_case_rings(&sw.base_draw,205.5f,205.5f);
                if(!mode) memcpy(reference,pixels,sizeof(pixels));
                else assert(!memcmp(reference,pixels,sizeof(pixels)));
            }
        }
        if(masked) { lv_draw_mask_remove_id(mask_id); lv_draw_mask_free_param(&mask); }
    }
    ring_cache_line=saved_line;
    puts("Ring cache: absent scratch and external-mask fallbacks match reference pixels.");
}
#endif
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

#ifdef CHRONVS_TEST_MOTHER_CACHE
static void test_mother_fallbacks(void) {
    static lv_color_t pixels[412 * 20], reference[412 * 20];
    lv_draw_sw_ctx_t sw;
    lv_draw_sw_init_ctx(NULL, &sw.base_draw);
    lv_color_t *saved_line = ring_cache_line;
    assert(saved_line);
    const lv_area_t clips[] = {{0,0,411,19}, {0,52,411,71}, {39,190,371,209},
                               {180,350,235,369}, {0,392,411,411}};
    const float centers[][2] = {{205.5f,205.5f}, {204.5f,205.5f}, {205.5f,185.5f}};
    for (int masked=0; masked<2; ++masked) {
        lv_draw_mask_radius_param_t mask;
        int16_t id = -1;
        if (masked) {
            const lv_area_t bounds = {20,20,391,391};
            lv_draw_mask_radius_init(&mask, &bounds, 185, false);
            id = lv_draw_mask_add(&mask, NULL); assert(id >= 0);
        }
        for (unsigned c=0;c<3;++c) for (unsigned i=0;i<5;++i) {
            lv_area_t buffer = {0,clips[i].y1,411,clips[i].y2};
            sw.base_draw.buf = pixels; sw.base_draw.buf_area = &buffer;
            sw.base_draw.clip_area = &clips[i];
            for (int mode=0;mode<3;++mode) {
                ring_cache_line = mode == 2 ? NULL : saved_line;
                for (unsigned x=0;x<412*20;++x) pixels[x] = lv_color_hex(COLOR_BEZEL_DARK);
                draw_case_rings(&sw.base_draw, centers[c][0], centers[c][1]);
                if (!mode) draw_circle(&sw.base_draw, centers[c][0], centers[c][1],
                                       154, COLOR_FACE, COLOR_TRACK, 1);
                else draw_mother_face(&sw.base_draw, centers[c][0], centers[c][1]);
                if (!mode) memcpy(reference,pixels,sizeof(pixels));
                else assert(!memcmp(reference,pixels,sizeof(pixels)));
            }
        }
        if (masked) { lv_draw_mask_remove_id(id); lv_draw_mask_free_param(&mask); }
    }
    ring_cache_line = saved_line;
    puts("Mother spans, partial clips, displaced centers, external mask and allocation fallback match.");
}
#endif

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
#ifdef CHRONVS_TEST_MOTHER_CACHE
    lv_obj_set_pos(clock_face, 0, 0);
    for (unsigned i=0;i<60;++i) {
        chronvs_time_t time = {.valid=true,.year=26,.month=10,.day=1+i%31,
            .weekday=i%7,.hour=i%24,.minute=i,.second=59-i};
        chronvs_watch_app_set_time(&time);
        lv_obj_invalidate(clock_face); lv_refr_now(display);
    }
    lv_obj_t *panel = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel,412,412);
    lv_obj_set_style_radius(panel,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x26302b),0);
    for (int direction=-1;direction<=1;direction+=2) for (int i=0;i<=10;++i) {
        lv_obj_set_pos(panel,0,direction*(412-i*41));
        lv_obj_invalidate(clock_face); lv_refr_now(display);
    }
    lv_obj_del(panel);
    circle_cover_count = 0;
#endif
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.free_biggest_size > 16384);
    assert(fclose(pixel_stream) == 0);
    printf("cache=%u checksum=%08x misses=%u large_misses=%u used=%u biggest=%u peak_sampled=%u smallest_sampled=%u\n",
           LV_CIRCLE_CACHE_SIZE, checksum, misses, large_misses,
           (unsigned)(memory.total_size-memory.free_size), (unsigned)memory.free_biggest_size,
           (unsigned)peak_used, (unsigned)smallest_block);
#if defined(CHRONVS_TEST_RING_CACHE) && !defined(CHRONVS_WATCH_RINGS_REFERENCE)
    test_ring_fallbacks();
#endif
#ifdef CHRONVS_TEST_MOTHER_CACHE
    test_mother_fallbacks();
#endif
    return 0;
}
