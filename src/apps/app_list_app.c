#include "apps/app_list_app.h"

#include <stdbool.h>
#include <stddef.h>

#include "apps/app_catalog.h"
#include "lvgl.h"

#define COLOR_PANEL       0x26302B
#define COLOR_PANEL_EDGE  0x748173
#define COLOR_TEXT        0xF2F2E9
#define COLOR_TEXT_DIM    0xB7C0B5
#define COLOR_ACCENT      0xF2B84B

#define SCREEN_CENTER 206
#define LIST_TOP 72
#define ROW_HEIGHT 74
#define SCREEN_SIZE 412
#define CLOSE_PULL_COMMIT_DISTANCE 120
#define CLOSE_PULL_START_Y 60
#define DRAG_FRAME_MS 20

typedef struct {
    const char *id;
} app_row_context_t;

static app_row_context_t row_contexts[8];
static int16_t gesture_start_y;
static bool close_pull_active;
static uint32_t last_back_drag_tick;

static int16_t curved_inset(int16_t row_center_y) {
    int16_t distance = row_center_y - SCREEN_CENTER;
    if (distance < 0) distance = -distance;
    return (int16_t)(24 + distance * 45 / SCREEN_CENTER);
}

static void open_app_event(lv_event_t *event) {
    const app_row_context_t *context = lv_event_get_user_data(event);
    if (context != NULL) chronvs_app_open(context->id);
}

static void place_active_app_at(int16_t x) {
    const uint32_t now = lv_tick_get();
    if (last_back_drag_tick != 0 &&
        lv_tick_elaps(last_back_drag_tick) < DRAG_FRAME_MS) {
        return;
    }
    last_back_drag_tick = now;
    chronvs_app_set_active_x(x);
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
            chronvs_app_set_active_y(dy);
            place_active_app_at(0);
        }
    }
    else if (code == LV_EVENT_PRESSING && close_pull_active) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        int16_t distance = point.y - gesture_start_y;
        if (distance < 0) distance = 0;
        if (distance > SCREEN_SIZE) distance = SCREEN_SIZE;
        place_active_app_at(0);
        chronvs_app_set_active_y(distance);
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

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "APLICATIVOS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    size_t visible_index = 0;
    const size_t count = chronvs_app_count();
    for (size_t index = 0; index < count && visible_index < 8; ++index) {
        const chronvs_app_t *app = chronvs_app_at(index);
        if (app == NULL || !app->launcher_visible) continue;

        const int16_t y = LIST_TOP + (int16_t)(visible_index * ROW_HEIGHT);
        const int16_t inset = curved_inset(y + 32);
        lv_obj_t *row = lv_btn_create(root);
        lv_obj_set_size(row, SCREEN_SIZE - 2 * inset, 64);
        lv_obj_set_pos(row, inset, y);
        lv_obj_set_style_radius(row, 32, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_PANEL_EDGE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        row_contexts[visible_index].id = app->id;
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(row, open_app_event, LV_EVENT_CLICKED,
                            &row_contexts[visible_index]);

        lv_obj_t *icon = lv_obj_create(row);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, 44, 44);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
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
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 82, 0);
        ++visible_index;
    }

    if (visible_index == 0) {
        lv_obj_t *empty_label = lv_label_create(root);
        lv_label_set_text(empty_label, "NENHUM APP INSTALADO");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_center(empty_label);
    }
    return root;
}

const chronvs_app_t chronvs_app_list_app = {
    .id = "apps",
    .name = "Aplicativos",
    .create_icon = NULL,
    .launcher_visible = false,
    .create = create_app_list,
    .on_show = NULL,
    .on_hide = NULL,
};

CHRONVS_REGISTER_APP(chronvs_app_list_app)
