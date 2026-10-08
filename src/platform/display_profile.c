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
static void (*original_render_start)(lv_disp_drv_t *);
static int64_t render_started_us, render_finished_us, latest_motion_us;
static uint32_t render_max_us, render_idle_max_us, motion_reads, motion_frames, motion_age_max_us;
static bool render_had_motion;
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

#ifdef CHRONVS_PANEL_PROFILE
/* One root per panel, bound once at creation. No allocations per draw. */
typedef struct {
    int64_t started_us;
    uint64_t draw_us, root_us, clip_pixels;
    uint32_t frames, slices, post_only;
    bool started, in_refresh;
} panel_profile_t;
static panel_profile_t panels[CHRONVS_PANEL_COUNT];

static void panel_draw_event(lv_event_t *event) {
    if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
    panel_profile_t *p = lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
        p->started_us = esp_timer_get_time();
        p->started = true;
        const lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
        p->clip_pixels += (uint64_t)lv_area_get_size(ctx->clip_area);
        ++p->slices;
    } else if (code == LV_EVENT_DRAW_MAIN_END && p->started) {
        p->root_us += (uint64_t)(esp_timer_get_time() - p->started_us);
    } else if (code == LV_EVENT_DRAW_POST_END) {
        if (p->started) {
            p->draw_us += (uint64_t)(esp_timer_get_time() - p->started_us);
            p->started = false;
            p->in_refresh = true;
        } else {
            /* LVGL can start at a covering child and only post-draw its parents. */
            ++p->post_only;
        }
    }
}

void chronvs_display_profile_bind_panel(lv_obj_t *root, chronvs_panel_t panel) {
    if (!root || (unsigned)panel >= CHRONVS_PANEL_COUNT) return;
    lv_obj_add_event_cb(root, panel_draw_event, LV_EVENT_DRAW_MAIN_BEGIN, &panels[panel]);
    lv_obj_add_event_cb(root, panel_draw_event, LV_EVENT_DRAW_MAIN_END, &panels[panel]);
    lv_obj_add_event_cb(root, panel_draw_event, LV_EVENT_DRAW_POST_END, &panels[panel]);
}
#endif

/* Nested timers must not increment watch_slices or watch_frames. */
int64_t chronvs_display_profile_watch_detail_begin(void) {
    return esp_timer_get_time();
}

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
    if (pressed && touching &&
        (data->point.x != last_point.x || data->point.y != last_point.y)) {
        ++motion_reads;
        latest_motion_us = now;
    }
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

static void profile_render_start(lv_disp_drv_t *drv) {
    const int64_t now = esp_timer_get_time();
    render_started_us = now;
    if (render_finished_us) {
        const uint32_t idle = (uint32_t)(now - render_finished_us);
        if (idle > render_idle_max_us) render_idle_max_us = idle;
    }
    render_had_motion = latest_motion_us != 0;
    if (render_had_motion) {
        const uint32_t age = (uint32_t)(now - latest_motion_us);
        if (age > motion_age_max_us) motion_age_max_us = age;
        latest_motion_us = 0;
    }
    if (original_render_start) original_render_start(drv);
}

static void profile_monitor(lv_disp_drv_t *drv, uint32_t elapsed, uint32_t px) {
    const int64_t now = esp_timer_get_time();
    if (render_started_us) {
        const uint32_t render = (uint32_t)(now - render_started_us);
        if (render > render_max_us) render_max_us = render;
        if (render_had_motion) ++motion_frames;
        render_finished_us = now;
        render_started_us = 0;
        render_had_motion = false;
    }
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
#ifdef CHRONVS_PANEL_PROFILE
    for (unsigned i = 0; i < CHRONVS_PANEL_COUNT; ++i) {
        if (panels[i].in_refresh) ++panels[i].frames;
        panels[i].in_refresh = false;
    }
#endif
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
    original_render_start = display->driver->render_start_cb;
    display->driver->flush_cb = profile_flush;
    display->driver->monitor_cb = profile_monitor;
    display->driver->render_start_cb = profile_render_start;
    for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) {
        if (input->driver->type != LV_INDEV_TYPE_POINTER || !input->driver->read_cb) continue;
        original_read = input->driver->read_cb;
        input->driver->read_cb = profile_read;
        break;
    }
    window_start = esp_timer_get_time();
#ifdef CHRONVS_WATCH_FLAT_BACKGROUND
    ESP_LOGI("display_perf", "watch_background=flat; orbital rendering disabled for A/B test");
#else
    ESP_LOGI("display_perf", "watch_background=orbital");
#endif
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
                 " watch_marker_us=%" PRIu64
#ifdef CHRONVS_WATCH_DETAIL_PROFILE
                 " watch_mother_face_us=%" PRIu64 " watch_mother_hand_us=%" PRIu64
                 " watch_mother_center_us=%" PRIu64
                 " watch_hours_face_us=%" PRIu64 " watch_hours_scale_us=%" PRIu64
                 " watch_hours_inner_us=%" PRIu64 " watch_hours_hand_us=%" PRIu64
#endif
                 " render_max_us=%" PRIu32 " render_idle_max_us=%" PRIu32
                 " motion_reads=%" PRIu32 " motion_frames=%" PRIu32
                 " motion_age_max_us=%" PRIu32
#ifdef CHRONVS_PANEL_PROFILE
                 " quick_frames=%" PRIu32 " quick_slices=%" PRIu32
                 " quick_clip_px=%" PRIu64 " quick_draw_us=%" PRIu64
                 " quick_root_us=%" PRIu64 " quick_post_only=%" PRIu32
                 " launcher_frames=%" PRIu32 " launcher_slices=%" PRIu32
                 " launcher_clip_px=%" PRIu64 " launcher_draw_us=%" PRIu64
                 " launcher_root_us=%" PRIu64 " launcher_post_only=%" PRIu32
#endif
                 ,
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
                 watch_us[CHRONVS_WATCH_MARKER],
#ifdef CHRONVS_WATCH_DETAIL_PROFILE
                 watch_us[CHRONVS_WATCH_MOTHER_FACE], watch_us[CHRONVS_WATCH_MOTHER_HAND],
                 watch_us[CHRONVS_WATCH_MOTHER_CENTER],
                 watch_us[CHRONVS_WATCH_HOURS_FACE], watch_us[CHRONVS_WATCH_HOURS_SCALE],
                 watch_us[CHRONVS_WATCH_HOURS_INNER], watch_us[CHRONVS_WATCH_HOURS_HAND],
#endif
                 render_max_us, render_idle_max_us,
                 motion_reads, motion_frames, motion_age_max_us
#ifdef CHRONVS_PANEL_PROFILE
                 , panels[0].frames, panels[0].slices, panels[0].clip_pixels,
                 panels[0].draw_us, panels[0].root_us, panels[0].post_only,
                 panels[1].frames, panels[1].slices, panels[1].clip_pixels,
                 panels[1].draw_us, panels[1].root_us, panels[1].post_only
#endif
                 );
    }
    frames = slow_frames = max_ms = 0;
    flush_us = refresh_ms = pixels = 0;
    window_start = now;
    touch_reads = touch_read_max_us = touch_gap_max_us = 0;
    interaction_frames = frame_gap_max_us = input_refresh_max_us = 0;
    last_touch_us = last_frame_us = pending_input_us = 0;
    render_started_us = render_finished_us = latest_motion_us = 0;
    render_max_us = render_idle_max_us = motion_reads = motion_frames = motion_age_max_us = 0;
    render_had_motion = false;
    for (unsigned i = 0; i < CHRONVS_WATCH_SECTION_COUNT; ++i) watch_us[i] = 0;
    watch_frames = watch_slices = 0;
    watch_in_refresh = false;
#ifdef CHRONVS_PANEL_PROFILE
    for (unsigned i = 0; i < CHRONVS_PANEL_COUNT; ++i) panels[i] = (panel_profile_t){0};
#endif
    if (discard) touching = false;
}
#endif
