#include "apps/app_list_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#include "apps/app_catalog.h"
#include "lvgl.h"
#include "ui/control_style.h"
#include "ui/app_input.h"
#include "ui/system_ui.h"
#include "ui/circle_clip.h"
#include "ui/panel_motion.h"
#include "platform/display_profile.h"
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
#include "esp_heap_caps.h"
#include <stdlib.h>
#endif

/* Keep the panel optimization isolated from drivers and other apps. */
#if defined(__GNUC__) && defined(CHRONVS_PANEL_CODE_O2)
#pragma GCC optimize ("O2")
#endif

#define COLOR_PANEL       0x26302B
#define COLOR_PANEL_EDGE  0x748173
#define COLOR_TEXT        0xF2F2E9
#define COLOR_TEXT_DIM    0xB7C0B5
#define COLOR_ACCENT      0xF2B84B

#define SCREEN_CENTER 206
#define LIST_TOP 0
#define ROW_HEIGHT 82
#define ROW_TOP 174
#define ARC_RADIUS 166
#define SCREEN_SIZE 412
#define CLOSE_PULL_COMMIT_DISTANCE 120
#define CLOSE_PULL_START_Y 60
#define DRAG_FRAME_MS 20
#define TAP_SLOP 8
#define SCROLL_TAP_GUARD_MS 150
#define SCROLL_INPUT_SMOOTH_MS 50
#define SCROLL_INPUT_MAX_LAG_PX 48
#define SCROLL_RELEASE_MAX_STEP 16
#define MAX_APP_ROWS 16

typedef struct {
    const char *id;
    int translate_x;
    lv_opa_t opacity;
    bool curve_set;
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
    lv_obj_t *vector_icon;
    lv_obj_t *cached_icon;
    lv_img_dsc_t cache;
#endif
} app_row_context_t;

#ifdef CHRONVS_LAUNCHER_ICON_CACHE
static void delete_icon_cache(lv_event_t *event) {
    app_row_context_t *context = lv_event_get_user_data(event);
    lv_img_cache_invalidate_src(&context->cache);
    free((void *)context->cache.data);
    context->cache.data = NULL;
    context->cached_icon = NULL;
}

static void cache_icon(app_row_context_t *context, lv_obj_t *icon) {
    const uint32_t size = lv_snapshot_buf_size_needed(icon, LV_IMG_CF_TRUE_COLOR);
    if (size != 44U * 44U * sizeof(lv_color_t)) return;
    lv_color_t *pixels = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels == NULL) return;
    const lv_color_t background = lv_color_hex(COLOR_PANEL_EDGE);
    lv_obj_set_style_bg_color(icon, background, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    const lv_res_t result = lv_snapshot_take_to_buf(icon, LV_IMG_CF_TRUE_COLOR,
                                                   &context->cache, pixels, size);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    if (result != LV_RES_OK) { free(pixels); return; }
    bool opaque = false;
#ifdef CHRONVS_LAUNCHER_ICON_OPAQUE
    /* A 40 px square fits wholly in the badge's opaque interior. Only trim
     * the 2 px border when every discarded pixel is unchanged background. */
    opaque = true;
    for (unsigned y = 0; y < 44 && opaque; ++y)
        for (unsigned x = 0; x < 44; ++x)
            if ((x < 2 || x >= 42 || y < 2 || y >= 42) && pixels[y * 44 + x].full != background.full) {
                opaque = false;
                break;
            }
    if (opaque) {
        for (unsigned y = 0; y < 40; ++y)
            memmove(pixels + y * 40, pixels + (y + 2) * 44 + 2, 40 * sizeof(*pixels));
        context->cache.header.w = context->cache.header.h = 40;
        context->cache.header.cf = LV_IMG_CF_TRUE_COLOR;
        context->cache.data_size = 40U * 40U * sizeof(*pixels);
    }
#endif
    /* Precompose AA on the fixed badge color; leave untouched pixels clear. */
    if (!opaque) {
        for (uint32_t i = 0; i < 44U * 44U; ++i) {
            if (pixels[i].full == LV_COLOR_CHROMA_KEY.full) {
                free(pixels); return; /* Never discard a real icon color. */
            }
        }
        for (uint32_t i = 0; i < 44U * 44U; ++i)
            if (pixels[i].full == background.full) pixels[i] = LV_COLOR_CHROMA_KEY;
        context->cache.header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
        context->cache.data_size = size;
    }
    context->vector_icon = icon;
    context->cached_icon = lv_img_create(lv_obj_get_parent(icon));
    lv_img_set_src(context->cached_icon, &context->cache);
    lv_obj_center(context->cached_icon);
    lv_obj_clear_flag(context->cached_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(context->cached_icon, delete_icon_cache, LV_EVENT_DELETE, context);
    lv_obj_add_flag(context->cached_icon, LV_OBJ_FLAG_HIDDEN);
}
#endif

static app_row_context_t row_contexts[MAX_APP_ROWS];
#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
static chronvs_circle_clip_t circle_clip;
static chronvs_circle_clip_node_t circle_nodes[MAX_APP_ROWS][3];
static chronvs_circle_clip_node_t empty_circle_node;
#endif
static lv_obj_t *rows[MAX_APP_ROWS];
static lv_obj_t *list;
static size_t row_count;
static lv_timer_t *curve_timer;
static bool curve_dirty;
static chronvs_ui_app_input_t activity;
static int16_t gesture_start_y;
static bool close_pull_active;
static uint32_t last_back_drag_tick;
static lv_point_t contact_start;
static lv_obj_t *contact_target;
static bool tap_candidate;
static bool user_scroll_seen;
static uint32_t last_user_scroll_tick;
static int32_t scroll_input_q8;
static int16_t scroll_input_y;
static int16_t scroll_throw_y;
static uint32_t scroll_input_tick;
static bool scroll_input_active;

static void smooth_scroll_input(lv_indev_t *input) {
    if (!input || !scroll_input_active) return;
    lv_point_t raw;
    lv_indev_get_point(input,&raw);
    const uint32_t now=lv_tick_get();
    uint32_t elapsed=now-scroll_input_tick;
    scroll_input_tick=now;
    if (elapsed>100) elapsed=100;
    const int32_t target=(int32_t)raw.y*256;
    const int32_t max_lag=SCROLL_INPUT_MAX_LAG_PX*256;
    int32_t delta=target-scroll_input_q8;
    if (delta>max_lag) scroll_input_q8=target-max_lag;
    else if (delta < -max_lag) scroll_input_q8=target+max_lag;
    delta=target-scroll_input_q8;
    if (delta<256 && delta > -256) scroll_input_q8=target;
    else scroll_input_q8 += delta*(int32_t)elapsed/(int32_t)(SCROLL_INPUT_SMOOTH_MS+elapsed);
    const int16_t next=(scroll_input_q8+128)/256;
    const int16_t step=next-scroll_input_y;
    scroll_input_y=next;
    scroll_throw_y=(scroll_throw_y+step)/2;
    /* LVGL 8.3.11 sends PRESSING after computing these fields and before its
     * scroll handler. Filter only the scroll vector; raw points still drive
     * tap rejection, hit testing and the top-edge close gesture. Native LVGL
     * retains row geometry, bounds, release processing and momentum. */
    input->proc.types.pointer.vect.y=step;
    input->proc.types.pointer.scroll_throw_vect.y=scroll_throw_y;
    input->proc.types.pointer.scroll_throw_vect_ori.y=scroll_throw_y;
}

static void track_tap_motion(lv_indev_t *input) {
    if (!input || !tap_candidate) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    int dx=point.x-contact_start.x, dy=point.y-contact_start.y;
    if (dx>TAP_SLOP || dx < -TAP_SLOP || dy>TAP_SLOP || dy < -TAP_SLOP)
        tap_candidate=false;
}

static void update_curve(lv_timer_t *timer) {
    (void)timer;
    if (!curve_dirty || chronvs_system_ui_display_is_off()) return;
    curve_dirty = false;
    int scroll = lv_obj_get_scroll_y(list);
    const int cycle = (int)row_count * ROW_HEIGHT;
    if (!cycle) return;
    const int phase = ((scroll % cycle) + cycle) % cycle;
    const int rebased = cycle + phase;
    /* Recycle outside the circular viewport, keeping native child geometry
     * and pointer scrolling. Padding reserves room in both directions; the
     * existing 20 ms timer bounds coordinates without accumulating distance. */
    for (size_t i = 0; i < row_count; ++i) {
        const int offset = ((int)i * ROW_HEIGHT - phase + cycle + cycle / 2) % cycle - cycle / 2;
        const int y = ROW_TOP + rebased + offset;
        if (lv_obj_get_y(rows[i]) != y) lv_obj_set_y(rows[i], y);
    }
    lv_obj_update_layout(list);
    if (scroll != rebased) lv_obj_scroll_to_y(list, rebased, LV_ANIM_OFF);
    scroll = rebased;
    for (size_t i = 0; i < row_count; ++i) {
        int dy = LIST_TOP + lv_obj_get_y(rows[i]) + 32 - scroll - SCREEN_CENTER;
        if (dy < 0) dy = -dy;
        const int clamped = dy > ARC_RADIUS ? ARC_RADIUS : dy;
        const int x = SCREEN_CENTER - 32 - (int)sqrtf(ARC_RADIUS * ARC_RADIUS - clamped * clamped);
        const lv_opa_t opacity = dy >= 178 ? LV_OPA_TRANSP :
            dy <= 130 ? LV_OPA_COVER : (178 - dy) * LV_OPA_COVER / 48;
        app_row_context_t *context = &row_contexts[i];
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
        if (context->cached_icon != NULL && (!context->curve_set ||
            (context->opacity == LV_OPA_COVER) != (opacity == LV_OPA_COVER))) {
            if (opacity == LV_OPA_COVER) {
                lv_obj_add_flag(context->vector_icon, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(context->cached_icon, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(context->vector_icon, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(context->cached_icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
#endif
        if (!context->curve_set || context->translate_x != x)
            lv_obj_set_style_translate_x(rows[i], x, 0);
        if (!context->curve_set || context->opacity != opacity) {
            lv_obj_set_style_opa_layered(rows[i],
                opacity == LV_OPA_TRANSP ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
            /* Composite only the 60 px badge: raw vector callbacks bypass
             * inherited opacity, and fading each stroke accumulates overlap. */
            lv_obj_set_style_opa_layered(lv_obj_get_child(rows[i], 0), opacity, 0);
            lv_obj_set_style_opa(lv_obj_get_child(rows[i], 1), opacity, 0);
            lv_obj_set_style_bg_opa(rows[i], opacity, LV_STATE_PRESSED);
        }
        /* Skip fully invisible subtrees before any custom drawing. */
        if (!context->curve_set ||
            (context->opacity == LV_OPA_TRANSP) != (opacity == LV_OPA_TRANSP))
            lv_obj_set_style_opa_layered(lv_obj_get_child(rows[i], 1),
#ifdef CHRONVS_PANEL_NO_CONTENT
                LV_OPA_TRANSP, 0);
#else
                opacity == LV_OPA_TRANSP ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
#endif
        context->translate_x = x;
        context->opacity = opacity;
        context->curve_set = true;
    }
}

static void scroll_event(lv_event_t *event) {
    (void)event;
    curve_dirty = true;
    if (lv_indev_get_act()) {
        tap_candidate = false;
        user_scroll_seen = true;
        last_user_scroll_tick = lv_tick_get();
    }
}

static void open_app_event(lv_event_t *event) {
    const app_row_context_t *context = lv_event_get_user_data(event);
    track_tap_motion(lv_indev_get_act());
    if (context != NULL && tap_candidate && context->opacity != LV_OPA_TRANSP &&
        contact_target == lv_event_get_target(event) &&
        !close_pull_active && !chronvs_app_is_settling()) {
        tap_candidate = false;
        chronvs_app_open(context->id);
    }
}

static void place_launcher_at(int16_t y) {
    const uint32_t now = lv_tick_get();
    if (last_back_drag_tick != 0 &&
        lv_tick_elaps(last_back_drag_tick) < DRAG_FRAME_MS) {
        return;
    }
    last_back_drag_tick = now;
    chronvs_app_set_active_x(0);
    chronvs_app_set_active_y(y);
}

static void app_list_touch_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED)
        track_tap_motion(lv_indev_get_act());
    if (code == LV_EVENT_PRESSING) smooth_scroll_input(lv_indev_get_act());
    if (code == LV_EVENT_PRESS_LOST) tap_candidate = false;

    if (code == LV_EVENT_PRESSED) {
        if (chronvs_app_is_settling()) {
            scroll_input_active = false;
            tap_candidate = false;
            chronvs_system_ui_notify_activity();
            lv_indev_t *input = lv_indev_get_act();
            if (input) lv_indev_wait_release(input);
            return;
        }
        lv_indev_get_point(lv_indev_get_act(), &point);
        contact_start = point;
        contact_target = lv_event_get_target(event);
        tap_candidate = !user_scroll_seen ||
                        lv_tick_elaps(last_user_scroll_tick) >= SCROLL_TAP_GUARD_MS;
        gesture_start_y = point.y;
        scroll_input_q8=(int32_t)point.y*256;
        scroll_input_y=point.y;
        scroll_throw_y=0;
        scroll_input_tick=lv_tick_get();
        scroll_input_active=gesture_start_y>CLOSE_PULL_START_Y;
        close_pull_active = false;
        last_back_drag_tick = 0;
    }
    else if (code == LV_EVENT_PRESSING && !close_pull_active) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        const int16_t dy = point.y - gesture_start_y;
        if (gesture_start_y <= CLOSE_PULL_START_Y &&
            dy > 20) {
            close_pull_active = true;
            tap_candidate = false;
            /* Keep the watch stationary underneath; only the launcher moves. */
            chronvs_app_preview_y("watch", 0);
            place_launcher_at(dy);
        }
    }
    else if (code == LV_EVENT_PRESSING && close_pull_active) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        int16_t distance = point.y - gesture_start_y;
        if (distance < 0) distance = 0;
        if (distance > SCREEN_SIZE) distance = SCREEN_SIZE;
        place_launcher_at(distance);
        chronvs_app_preview_y("watch", 0);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_indev_t *input=lv_indev_get_act();
        if (scroll_input_active && input && lv_indev_get_scroll_obj(input)==list) {
            /* Bound only the coast after release. A sparse final sample must
             * not fling through the list; finger-driven movement stays 1:1
             * with the filtered input, without changing the shared driver. */
            int velocity=input->proc.types.pointer.scroll_throw_vect.y;
            if (velocity>SCROLL_RELEASE_MAX_STEP) velocity=SCROLL_RELEASE_MAX_STEP;
            if (velocity < -SCROLL_RELEASE_MAX_STEP) velocity=-SCROLL_RELEASE_MAX_STEP;
            input->proc.types.pointer.scroll_throw_vect.y=velocity;
            input->proc.types.pointer.scroll_throw_vect_ori.y=velocity;
        }
        scroll_input_active = false;
        if (close_pull_active) {
            lv_indev_get_point(lv_indev_get_act(), &point);
            if (point.y - gesture_start_y >= CLOSE_PULL_COMMIT_DISTANCE) {
                chronvs_app_settle_y("apps", SCREEN_SIZE, "watch", CHRONVS_UI_PANEL_SETTLE_MS,
                                     CHRONVS_UI_PANEL_SETTLE_PATH);
            } else {
                chronvs_app_settle_y("apps", 0, "apps", CHRONVS_UI_PANEL_SETTLE_MS,
                                     CHRONVS_UI_PANEL_SETTLE_PATH);
            }
            close_pull_active = false;
        }
    }
}

static void show(void) {
    scroll_input_active = false;
    tap_candidate = false;
    curve_dirty = true;
    update_curve(NULL);
    lv_timer_resume(curve_timer);
}

static void hide(void) {
    scroll_input_active = false;
    tap_candidate = false;
    lv_timer_pause(curve_timer);
}

static lv_obj_t *create_app_list(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(root, true, 0);
#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
    chronvs_circle_clip_init(&circle_clip, root);
#endif
    lv_obj_add_flag(root, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, app_list_touch_event, LV_EVENT_ALL, NULL);

    list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, SCREEN_SIZE, SCREEN_SIZE - LIST_TOP);
    lv_obj_set_pos(list, 0, LIST_TOP);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    /* Isolation trial: keep filtered drag, but stop at release. */
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN |
                            LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(list, scroll_event, LV_EVENT_SCROLL, NULL);

    const chronvs_app_t *visible_apps[8];
    size_t visible_count = 0;
    const size_t count = chronvs_app_count();
    for (size_t index = 0; index < count && visible_count < 8; ++index) {
        const chronvs_app_t *app = chronvs_app_at(index);
        if (app == NULL || !app->launcher_visible) continue;
        visible_apps[visible_count++] = app;
    }
    /* A small catalog repeats whole cycles so recycling is always offscreen. */
    size_t slots = visible_count;
    while (slots && slots * ROW_HEIGHT < SCREEN_SIZE + ROW_HEIGHT) slots += visible_count;
    lv_obj_set_style_pad_bottom(list, 3 * slots * ROW_HEIGHT + SCREEN_SIZE, 0);
    size_t visible_index = 0;
    for (; visible_index < slots; ) {
        const chronvs_app_t *app = visible_apps[visible_index % visible_count];

        const int16_t y = ROW_TOP + (int16_t)(visible_index * ROW_HEIGHT);
        lv_obj_t *row = lv_btn_create(list);
        /* Pointer scrolling owns the list position. Focusing a partly clipped
         * row must not start a second scroll animation under the finger. */
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        chronvs_ui_style_control(row, false);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_size(row, 290, 64);
        lv_obj_set_pos(row, 0, y);
        rows[visible_index] = row;
        row_contexts[visible_index].id = app->id;
        row_contexts[visible_index].curve_set = false;
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(row, open_app_event, LV_EVENT_CLICKED,
                            &row_contexts[visible_index]);

        lv_obj_t *badge = lv_obj_create(row);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 60, 60);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(COLOR_PANEL_EDGE), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *icon = lv_obj_create(badge);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, 44, 44);
        lv_obj_center(icon);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        if (app->create_icon != NULL) {
            app->create_icon(icon);
        }
        else {
            lv_obj_t *fallback = lv_label_create(icon);
            lv_label_set_text(fallback, "?");
            lv_obj_set_style_text_font(fallback, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(fallback, lv_color_hex(COLOR_ACCENT), 0);
            lv_obj_center(fallback);
        }
#if defined(CHRONVS_PANEL_NO_CONTENT) || defined(CHRONVS_PANEL_NO_ICONS)
        /* Omit drawing only; keep geometry, callbacks and hit targets. */
        lv_obj_set_style_opa_layered(icon, LV_OPA_TRANSP, 0);
#endif
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
        cache_icon(&row_contexts[visible_index], icon);
#endif

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, app->name != NULL ? app->name : app->id);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_width(name, 214);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 76, 0);
#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
        chronvs_circle_clip_bind(&circle_clip, row, &circle_nodes[visible_index][0], true);
        chronvs_circle_clip_bind(&circle_clip, badge, &circle_nodes[visible_index][1], false);
        chronvs_circle_clip_bind(&circle_clip, name, &circle_nodes[visible_index][2], false);
#endif
        ++visible_index;
    }

    if (visible_index == 0) {
        lv_obj_t *empty_label = lv_label_create(root);
        lv_label_set_text(empty_label, "NENHUM APP INSTALADO");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_center(empty_label);
#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
        chronvs_circle_clip_bind(&circle_clip, empty_label, &empty_circle_node, false);
#endif
    }
    row_count = visible_index;
    /* Capture the close gesture without clipping items behind this clear strip. */
    lv_obj_t *close_zone = lv_obj_create(root);
    lv_obj_remove_style_all(close_zone);
    lv_obj_set_size(close_zone, SCREEN_SIZE, CLOSE_PULL_START_Y);
    lv_obj_clear_flag(close_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(close_zone, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
    chronvs_ui_app_input_bind(root, &activity);
    curve_timer = lv_timer_create(update_curve, DRAG_FRAME_MS, NULL);
    lv_timer_pause(curve_timer);
    lv_obj_update_layout(root);
    lv_obj_scroll_to_y(list, row_count ? (row_count - 1) * ROW_HEIGHT / 2 : 0, LV_ANIM_OFF);
    curve_dirty = true;
    update_curve(NULL);
    CHRONVS_PANEL_PROFILE_BIND(root, LAUNCHER);
    return root;
}

const chronvs_app_t chronvs_app_list_app = {
    .id = "apps",
    .name = "Aplicativos",
    .create_icon = NULL,
    .launcher_visible = false,
    .create = create_app_list,
    .on_show = show,
    .on_hide = hide,
};

CHRONVS_REGISTER_APP(chronvs_app_list_app)
