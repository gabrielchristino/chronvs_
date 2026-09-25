#include "platform/display_profile.h"

#ifdef CHRONVS_DISPLAY_PROFILE
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

/* UI task only; no allocation or logging per flush. */
static void (*original_flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);
static void (*original_monitor)(lv_disp_drv_t *, uint32_t, uint32_t);
static uint64_t flush_us, refresh_ms, pixels;
static uint32_t frames, slow_frames, max_ms;
static int64_t window_start;
static bool was_off;
static void (*original_read)(lv_indev_drv_t *, lv_indev_data_t *);
static uint32_t touch_reads, touch_read_max_us, touch_gap_max_us;
static uint32_t interaction_frames, frame_gap_max_us, input_refresh_max_us;
static int64_t last_touch_us, last_frame_us, pending_input_us;
static bool touching;
static lv_point_t last_point;
static uint64_t watch_us[CHRONVS_WATCH_SECTION_COUNT];
static uint32_t watch_frames, watch_slices;
static bool watch_in_refresh;

int64_t chronvs_display_profile_watch_begin(void) {
    ++watch_slices;
    watch_in_refresh = true;
    return esp_timer_get_time();
}

int64_t chronvs_display_profile_watch_mark(chronvs_watch_section_t section, int64_t start) {
    const int64_t now = esp_timer_get_time();
    if ((unsigned)section < CHRONVS_WATCH_SECTION_COUNT && now >= start)
        watch_us[section] += (uint64_t)(now - start);
    return now;
}

static void profile_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    const int64_t start = esp_timer_get_time();
    original_read(drv, data);
    const int64_t now = esp_timer_get_time();
    const uint32_t elapsed = (uint32_t)(now - start);
    if (elapsed > touch_read_max_us) touch_read_max_us = elapsed;
    ++touch_reads;
    const bool pressed = data->state == LV_INDEV_STATE_PRESSED;
    if (pressed && touching && last_touch_us) {
        const uint32_t gap = (uint32_t)(now - last_touch_us);
        if (gap > touch_gap_max_us) touch_gap_max_us = gap;
    }
    if ((pressed != touching || (pressed &&
        (data->point.x != last_point.x || data->point.y != last_point.y))) && !pending_input_us)
        pending_input_us = now;
    if (!pressed || !touching) last_frame_us = 0;
    touching = pressed;
    last_touch_us = now;
    last_point = data->point;
}

static void profile_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
    const int64_t start = esp_timer_get_time();
    original_flush(drv, area, colors);
    flush_us += (uint64_t)(esp_timer_get_time() - start);
}

static void profile_monitor(lv_disp_drv_t *drv, uint32_t elapsed, uint32_t px) {
    const int64_t now = esp_timer_get_time();
    if (touching) {
        ++interaction_frames;
        if (last_frame_us) {
            const uint32_t gap = (uint32_t)(now - last_frame_us);
            if (gap > frame_gap_max_us) frame_gap_max_us = gap;
        }
        last_frame_us = now;
    }
    if (pending_input_us) {
        const uint32_t age = (uint32_t)(now - pending_input_us);
        if (age > input_refresh_max_us) input_refresh_max_us = age;
        pending_input_us = 0;
    }
    ++frames;
    if (watch_in_refresh) ++watch_frames;
    watch_in_refresh = false;
    refresh_ms += elapsed;
    pixels += px;
    if (elapsed > 20) ++slow_frames;
    if (elapsed > max_ms) max_ms = elapsed;
    if (original_monitor) original_monitor(drv, elapsed, px);
}

void chronvs_display_profile_init(void) {
    lv_disp_t *display = lv_disp_get_default();
    if (!display || !display->driver->flush_cb || original_flush) return;
    original_flush = display->driver->flush_cb;
    original_monitor = display->driver->monitor_cb;
    display->driver->flush_cb = profile_flush;
    display->driver->monitor_cb = profile_monitor;
    for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) {
        if (input->driver->type != LV_INDEV_TYPE_POINTER || !input->driver->read_cb) continue;
        original_read = input->driver->read_cb;
        input->driver->read_cb = profile_read;
        break;
    }
    window_start = esp_timer_get_time();
    ESP_LOGI("display_perf", "enabled; refresh includes synchronous flush; LVGL optimization=%s",
             CHRONVS_LVGL_OPT_LABEL);
}

void chronvs_display_profile_poll(bool display_off) {
    const int64_t now = esp_timer_get_time();
    const bool discard = display_off || was_off;
    was_off = display_off;
    if (!discard && now - window_start < 2000000) return;
    if (!discard && frames) {
        ESP_LOGI("display_perf",
                 "window_ms=%" PRIi64 " frames=%" PRIu32
                 " refresh_avg_ms=%" PRIu64 " refresh_max_ms=%" PRIu32
                 " over20=%" PRIu32 " flush_avg_us=%" PRIu64
                 " px_avg=%" PRIu64 " internal_free=%u dma_largest=%u"
                 " touch_reads=%" PRIu32 " touch_read_max_us=%" PRIu32
                 " touch_gap_max_us=%" PRIu32 " interaction_frames=%" PRIu32
                 " frame_gap_max_us=%" PRIu32 " input_refresh_max_us=%" PRIu32
                 " watch_frames=%" PRIu32 " watch_slices=%" PRIu32
                 " watch_setup_us=%" PRIu64 " watch_background_us=%" PRIu64
                 " watch_geometry_us=%" PRIu64 " watch_case_us=%" PRIu64
                 " watch_rings_us=%" PRIu64 " watch_dates_us=%" PRIu64
                 " watch_mother_us=%" PRIu64 " watch_minutes_us=%" PRIu64
                 " watch_hours_us=%" PRIu64 " watch_weekday_us=%" PRIu64
                 " watch_temperature_us=%" PRIu64 " watch_seconds_us=%" PRIu64
                 " watch_marker_us=%" PRIu64,
                 (now - window_start) / 1000, frames, refresh_ms / frames,
                 max_ms, slow_frames, flush_us / frames, pixels / frames,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                 touch_reads, touch_read_max_us, touch_gap_max_us, interaction_frames,
                 frame_gap_max_us, input_refresh_max_us, watch_frames, watch_slices,
                 watch_us[CHRONVS_WATCH_SETUP], watch_us[CHRONVS_WATCH_BACKGROUND],
                 watch_us[CHRONVS_WATCH_GEOMETRY],
                 watch_us[CHRONVS_WATCH_RINGS] + watch_us[CHRONVS_WATCH_DATES],
                 watch_us[CHRONVS_WATCH_RINGS], watch_us[CHRONVS_WATCH_DATES],
                 watch_us[CHRONVS_WATCH_MOTHER], watch_us[CHRONVS_WATCH_MINUTES],
                 watch_us[CHRONVS_WATCH_HOURS], watch_us[CHRONVS_WATCH_WEEKDAY],
                 watch_us[CHRONVS_WATCH_TEMPERATURE], watch_us[CHRONVS_WATCH_SECONDS],
                 watch_us[CHRONVS_WATCH_MARKER]);
    }
    frames = slow_frames = max_ms = 0;
    flush_us = refresh_ms = pixels = 0;
    window_start = now;
    touch_reads = touch_read_max_us = touch_gap_max_us = 0;
    interaction_frames = frame_gap_max_us = input_refresh_max_us = 0;
    last_touch_us = last_frame_us = pending_input_us = 0;
    for (unsigned i = 0; i < CHRONVS_WATCH_SECTION_COUNT; ++i) watch_us[i] = 0;
    watch_frames = watch_slices = 0;
    watch_in_refresh = false;
    if (discard) touching = false;
}
#endif
