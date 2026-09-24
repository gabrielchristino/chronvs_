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

static void profile_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
    const int64_t start = esp_timer_get_time();
    original_flush(drv, area, colors);
    flush_us += (uint64_t)(esp_timer_get_time() - start);
}

static void profile_monitor(lv_disp_drv_t *drv, uint32_t elapsed, uint32_t px) {
    ++frames;
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
                 " px_avg=%" PRIu64 " internal_free=%u dma_largest=%u",
                 (now - window_start) / 1000, frames, refresh_ms / frames,
                 max_ms, slow_frames, flush_us / frames, pixels / frames,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    }
    frames = slow_frames = max_ms = 0;
    flush_us = refresh_ms = pixels = 0;
    window_start = now;
}
#endif
