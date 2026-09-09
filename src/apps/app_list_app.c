#include "apps/app_list_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <math.h>

#include "apps/app_catalog.h"
#include "lvgl.h"
#include "ui/control_style.h"
#include "ui/app_input.h"
#include "ui/system_ui.h"

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

typedef struct {
    const char *id;
} app_row_context_t;

static app_row_context_t row_contexts[8];
static lv_obj_t *rows[8];
static lv_obj_t *list;
static size_t row_count;
static lv_timer_t *curve_timer;
static bool curve_dirty;
static chronvs_ui_app_input_t activity;
static int16_t gesture_start_y;
static bool close_pull_active;
static uint32_t last_back_drag_tick;

static void update_curve(lv_timer_t *timer) {
    (void)timer;
    if (!curve_dirty || chronvs_system_ui_display_is_off()) return;
    curve_dirty = false;
    const int scroll = lv_obj_get_scroll_y(list);
    for (size_t i = 0; i < row_count; ++i) {
        int dy = LIST_TOP + ROW_TOP + (int)i * ROW_HEIGHT + 32 - scroll - SCREEN_CENTER;
        if (dy < 0) dy = -dy;
        const int clamped = dy > ARC_RADIUS ? ARC_RADIUS : dy;
        const int x = SCREEN_CENTER - 32 - (int)sqrtf(ARC_RADIUS * ARC_RADIUS - clamped * clamped);
        lv_obj_set_style_translate_x(rows[i], x, 0);
        lv_obj_set_style_opa(rows[i], dy >= 178 ? LV_OPA_TRANSP :
            dy <= 130 ? LV_OPA_COVER : (178 - dy) * LV_OPA_COVER / 48, 0);
    }
}

static void scroll_event(lv_event_t *event) {
    (void)event;
    curve_dirty = true;
}

static void open_app_event(lv_event_t *event) {
    const app_row_context_t *context = lv_event_get_user_data(event);
    if (context != NULL && !close_pull_active) chronvs_app_open(context->id);
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

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        gesture_start_y = point.y;
        close_pull_active = false;
        last_back_drag_tick = 0;
    }
    else if (code == LV_EVENT_PRESSING && !close_pull_active) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        const int16_t dy = point.y - gesture_start_y;
        if (gesture_start_y <= CLOSE_PULL_START_Y &&
            dy > 20) {
            close_pull_active = true;
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
        if (close_pull_active) {
            lv_indev_get_point(lv_indev_get_act(), &point);
            if (point.y - gesture_start_y >= CLOSE_PULL_COMMIT_DISTANCE) {
                chronvs_app_open("watch");
            } else {
                chronvs_app_cancel_preview();
            }
            close_pull_active = false;
        }
    }
}

static void show(void) {
    curve_dirty = true;
    update_curve(NULL);
    lv_timer_resume(curve_timer);
}

static void hide(void) { lv_timer_pause(curve_timer); }

static lv_obj_t *create_app_list(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, app_list_touch_event, LV_EVENT_ALL, NULL);

    list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, SCREEN_SIZE, SCREEN_SIZE - LIST_TOP);
    lv_obj_set_pos(list, 0, LIST_TOP);
    lv_obj_set_style_pad_bottom(list, 174, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(list, scroll_event, LV_EVENT_SCROLL, NULL);

    size_t visible_index = 0;
    const size_t count = chronvs_app_count();
    for (size_t index = 0; index < count && visible_index < 8; ++index) {
        const chronvs_app_t *app = chronvs_app_at(index);
        if (app == NULL || !app->launcher_visible) continue;

        const int16_t y = ROW_TOP + (int16_t)(visible_index * ROW_HEIGHT);
        lv_obj_t *row = lv_btn_create(list);
        chronvs_ui_style_control(row, false);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_size(row, 290, 64);
        lv_obj_set_pos(row, 0, y);
        rows[visible_index] = row;
        row_contexts[visible_index].id = app->id;
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
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

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, app->name != NULL ? app->name : app->id);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_width(name, 214);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 76, 0);
        ++visible_index;
    }

    if (visible_index == 0) {
        lv_obj_t *empty_label = lv_label_create(root);
        lv_label_set_text(empty_label, "NENHUM APP INSTALADO");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_center(empty_label);
    }
    row_count = visible_index;
    /* Capture the close gesture without clipping items behind this clear strip. */
    lv_obj_t *close_zone = lv_obj_create(root);
    lv_obj_remove_style_all(close_zone);
    lv_obj_set_size(close_zone, SCREEN_SIZE, CLOSE_PULL_START_Y);
    lv_obj_clear_flag(close_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(close_zone, LV_OBJ_FLAG_EVENT_BUBBLE);
    chronvs_ui_app_input_bind(root, &activity);
    curve_timer = lv_timer_create(update_curve, DRAG_FRAME_MS, NULL);
    lv_timer_pause(curve_timer);
    lv_obj_update_layout(root);
    lv_obj_scroll_to_y(list, row_count ? (row_count - 1) * ROW_HEIGHT / 2 : 0, LV_ANIM_OFF);
    curve_dirty = true;
    update_curve(NULL);
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
