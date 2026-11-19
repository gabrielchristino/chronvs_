#include "platform/panel_autotest.h"
#ifdef CHRONVS_PANEL_AUTOTEST
#include <inttypes.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "core/app_manager.h"
#include "services/sound_service.h"
#include "ui/system_ui.h"
#include "platform/display_profile.h"
#ifdef CHRONVS_PANEL_AUTOTEST_WEATHER
#include "esp_heap_caps.h"
#include "services/weather_service.h"
static bool checking_weather, saw_weather_fetching;
static uint32_t weather_tick;
static size_t weather_free_min, weather_dma_min;
#endif

typedef struct {
    const char *name;
    int16_t from, to;
    bool quick, expect_open;
    const char *app;
} gesture_t;
static const gesture_t gestures[] = {
    {"quick_open",20,230,true,true,"watch"},
    {"quick_close",200,60,true,false,"watch"},
    {"quick_cancel_open",20,90,true,false,"watch"},
    {"launcher_open",300,60,false,true,"apps"},
    {"launcher_cancel_close",30,100,false,true,"apps"},
    {"launcher_close",30,230,false,false,"watch"},
    {"launcher_cancel_open",300,245,false,false,"watch"},
};
enum { CASES = sizeof(gestures)/sizeof(gestures[0]), TOTAL = CASES*2*3 };
static const gesture_t wake = {"wake",206,206,false,false,"watch"};
static lv_indev_t *pointer;
static void (*previous_read)(lv_indev_drv_t *,lv_indev_data_t *);
static void (*previous_flush)(lv_disp_drv_t *,const lv_area_t *,lv_color_t *);
static void (*previous_start)(lv_disp_drv_t *);
static void (*previous_monitor)(lv_disp_drv_t *,uint32_t,uint32_t);
static const gesture_t *gesture;
static bool running, started, waking, press_seen, end_seen, release_seen, abort_requested;
static unsigned completed, failed, moves;
static uint32_t startup_tick, case_tick, press_tick, release_tick, movement_ms, last_read_tick;
static uint32_t press_gap_max, frames, slices, frame_max;
static int min_y, max_y, previous_y;
static bool position_seen;
static uint64_t render_us, flush_us, pixels, frame_flush;
static int64_t frame_start;
static chronvs_ui_panel_probe_t preferences;
static uint8_t volume;

static void finish(bool abort) {
    if (abort) ++failed;
    running = false;
    pointer->driver->read_cb = previous_read;
    lv_indev_reset(pointer,NULL);
    lv_indev_wait_release(pointer);
    chronvs_display_profile_dump_panel_parts();
    ESP_LOGI("panel_test","DONE completed=%u total=%u failed=%u physical_touch_restored=1",
        completed,TOTAL,failed);
}

static void read_script(lv_indev_drv_t *drv,lv_indev_data_t *data) {
    (void)drv;
    data->state = LV_INDEV_STATE_REL;
    data->point = (lv_point_t){206,206};
    if (!running || !started || !gesture || release_seen) return;
    /* Never deliver a scripted touch to an alarm or the power menu. The wake
     * guard is allowed to consume the initial wake-only contact. */
    if (!chronvs_system_ui_display_is_off() && lv_obj_get_child_cnt(lv_layer_top())) {
        abort_requested = true; return;
    }
    const uint32_t now = lv_tick_get();
    data->point.y = gesture->to;
    if (end_seen) { release_seen = true; release_tick = now; return; }
    if (!press_seen) { press_seen = true; press_tick = now; last_read_tick = now; }
    const uint32_t gap = now-last_read_tick;
    if (gap > press_gap_max) press_gap_max = gap;
    last_read_tick = now;
    const uint32_t held = now-press_tick;
    const uint32_t elapsed = held > 90 ? held-90 : 0;
    const uint32_t position = elapsed > movement_ms ? movement_ms : elapsed;
    data->state = LV_INDEV_STATE_PR;
    data->point.y = gesture->from + (int32_t)(gesture->to-gesture->from)*(int32_t)position/(int32_t)movement_ms;
    if (elapsed >= movement_ms) end_seen = true; /* Force endpoint PR before REL. */
}

static void render_start(lv_disp_drv_t *drv) {
    frame_start = esp_timer_get_time(); frame_flush = 0;
    if (previous_start) previous_start(drv);
}
static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *colors) {
    const int64_t began = esp_timer_get_time();
    previous_flush(drv,area,colors); /* Includes original synchronous QSPI drain. */
    const uint64_t elapsed = esp_timer_get_time()-began;
    frame_flush += elapsed;
    if (running && started && !waking) { flush_us += elapsed; ++slices; }
}
static void monitor(lv_disp_drv_t *drv,uint32_t elapsed,uint32_t px) {
    const uint32_t duration = (uint32_t)(esp_timer_get_time()-frame_start);
    if (running && started && !waking) {
        ++frames; pixels += px;
        if (duration > frame_max) frame_max = duration;
        if (duration >= frame_flush) render_us += duration-frame_flush;
    }
    if (previous_monitor) previous_monitor(drv,elapsed,px);
}

static void begin(const gesture_t *next) {
    gesture = next;
    case_tick = lv_tick_get();
    press_seen = end_seen = release_seen = false;
    frames = slices = frame_max = press_gap_max = moves = 0;
    render_us = flush_us = pixels = 0;
    position_seen = false; min_y = 1000; max_y = -1000;
    movement_ms = waking ? 90 : completed/CASES%2 ? 180 : 900;
    ESP_LOGI("panel_test","BEGIN n=%u name=%s speed=%s repeat=%u duration_ms=%" PRIu32,
        completed+1,next->name,waking ? "wake" : completed/CASES%2 ? "fast" : "slow",
        completed/(2*CASES)+1,movement_ms);
}

static lv_obj_t *launcher(void) {
    lv_obj_t *layer = chronvs_app_content_layer();
    for (unsigned i=0;i<lv_obj_get_child_cnt(layer);++i) {
        lv_obj_t *root = lv_obj_get_child(layer,i);
        if (lv_obj_get_style_radius(root,0)==LV_RADIUS_CIRCLE) return root;
    }
    return NULL;
}

void chronvs_panel_autotest_init(void) {
    if (pointer) return;
    for (lv_indev_t *p=lv_indev_get_next(NULL);p;p=lv_indev_get_next(p))
        if (lv_indev_get_type(p)==LV_INDEV_TYPE_POINTER) { pointer=p; break; }
    lv_disp_t *display=lv_disp_get_default();
    if (!pointer || !display || !display->driver->flush_cb) return;
    previous_read=pointer->driver->read_cb;
    previous_flush=display->driver->flush_cb;
    previous_start=display->driver->render_start_cb;
    previous_monitor=display->driver->monitor_cb;
    pointer->driver->read_cb=read_script;
    display->driver->flush_cb=flush;
    display->driver->render_start_cb=render_start;
    display->driver->monitor_cb=monitor;
    lv_indev_reset(pointer,NULL);
    running=true; startup_tick=lv_tick_get();
    ESP_LOGI("panel_test","READY total=%u startup_ms=8000 input=scripted real_gesture_handlers=1 TE=off",TOTAL);
}

void chronvs_panel_autotest_poll(void) {
    if (!running) return;
    if (abort_requested) { ESP_LOGE("panel_test","ABORT modal or power menu"); finish(true); return; }
#ifdef CHRONVS_PANEL_AUTOTEST_WEATHER
    if (checking_weather) {
        if (chronvs_system_ui_display_is_off() || lv_obj_get_child_cnt(lv_layer_top())) {
            ESP_LOGE("panel_test","WEATHER aborted: display off or modal"); finish(true); return;
        }
        const size_t available = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (available < weather_free_min) weather_free_min = available;
        if (largest < weather_dma_min) weather_dma_min = largest;
        const chronvs_weather_state_t state = chronvs_weather_state();
        if (state == CHRONVS_WEATHER_FETCHING) saw_weather_fetching = true;
        const bool timeout = lv_tick_elaps(weather_tick) > 60000;
        if ((state == CHRONVS_WEATHER_SUCCESS || state == CHRONVS_WEATHER_ERROR || timeout) &&
            lv_tick_elaps(weather_tick) > 2000) {
            const bool passed = state == CHRONVS_WEATHER_SUCCESS && saw_weather_fetching && !timeout;
            ESP_LOGI("panel_test","WEATHER pass=%u state=%u fetching_seen=%u timeout=%u internal_free_min=%u dma_largest_min=%u",
                passed,(unsigned)state,saw_weather_fetching,timeout,(unsigned)weather_free_min,(unsigned)weather_dma_min);
            chronvs_app_open("watch");
            finish(!passed);
        }
        return;
    }
#endif
    if (!started) {
        if (lv_tick_elaps(startup_tick)<8000) return;
        chronvs_app_open("watch");
        started=waking=true; begin(&wake); return;
    }
    if (lv_tick_elaps(case_tick)>movement_ms+5000) {
        ESP_LOGE("panel_test","ABORT gesture input timeout"); finish(true); return;
    }
    const chronvs_ui_panel_probe_t probe=chronvs_system_ui_panel_probe();
    lv_obj_t *apps=launcher();
    const bool visible=gesture->quick ? !probe.hidden : apps && !lv_obj_has_flag(apps,LV_OBJ_FLAG_HIDDEN);
    const int y=gesture->quick ? probe.y : apps ? lv_obj_get_y(apps) : 0;
    if (!waking && visible) {
        if (position_seen && y!=previous_y) ++moves;
        if (y<min_y) min_y=y;
        if (y>max_y) max_y=y;
        previous_y=y; position_seen=true;
    }
    if (!release_seen || lv_tick_elaps(release_tick)<650) return;
    if (waking) {
        waking=false; preferences=probe; volume=chronvs_sound_volume();
        begin(&gestures[0]); return;
    }
    const bool same_preferences=probe.brightness==preferences.brightness &&
        probe.profile==preferences.profile && probe.eco==preferences.eco && chronvs_sound_volume()==volume;
    const bool correct_panel=gesture->quick ? (gesture->expect_open ? !probe.hidden && probe.y==0 : probe.hidden && probe.y==-412) :
        probe.hidden && (gesture->expect_open ? apps && !lv_obj_has_flag(apps,LV_OBJ_FLAG_HIDDEN) && y==0 : !apps || lv_obj_has_flag(apps,LV_OBJ_FLAG_HIDDEN));
    const char *active=chronvs_app_active_id();
    const bool passed=correct_panel && active && !strcmp(active,gesture->app) &&
        same_preferences && !chronvs_system_ui_display_is_off() && frames && moves && max_y-min_y>=10;
    ESP_LOGI("panel_test","RESULT n=%u name=%s speed=%s repeat=%u pass=%u frames=%" PRIu32
        " slices=%" PRIu32 " px_avg=%" PRIu64 " render_avg_us=%" PRIu64 " flush_avg_us=%" PRIu64
        " frame_max_us=%" PRIu32 " press_ms=%" PRIu32 " touch_gap_max_ms=%" PRIu32
        " moves=%u y_min=%d y_max=%d app=%s quick_y=%d hidden=%u preferences_ok=%u",
        completed+1,gesture->name,completed/CASES%2 ? "fast" : "slow",completed/(2*CASES)+1,passed,
        frames,slices,frames ? pixels/frames : 0,frames ? render_us/frames : 0,frames ? flush_us/frames : 0,
        frame_max,release_tick-press_tick,press_gap_max,moves,min_y,max_y,active ? active : "none",probe.y,probe.hidden,same_preferences);
    ++completed;
    if (!passed) { finish(true); return; }
    if (completed==TOTAL) {
#ifdef CHRONVS_PANEL_AUTOTEST_WEATHER
        checking_weather = true;
        weather_tick = lv_tick_get();
        weather_free_min = weather_dma_min = SIZE_MAX;
        ESP_LOGI("panel_test","WEATHER BEGIN normal_app=1 real_https=1");
        if (!chronvs_app_open("weather")) finish(true);
#else
        finish(false);
#endif
        return;
    }
    begin(&gestures[completed%CASES]);
}
chronvs_panel_test_status_t chronvs_panel_autotest_status(void) {
    return (chronvs_panel_test_status_t){running,completed,failed,TOTAL};
}
#endif
