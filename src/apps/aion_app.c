#include "apps/app_catalog.h"

#include <stdbool.h>
#include <stdint.h>

#include "core/app_manager.h"
#include "lvgl.h"
#include "ui/system_ui.h"

#define COLOR_PANEL       0x26302B
#define COLOR_PANEL_EDGE  0x748173
#define COLOR_TEXT        0xF2F2E9
#define COLOR_TEXT_DIM    0xB7C0B5
#define COLOR_ACCENT      0xF2B84B
#define SCREEN_SIZE 412
#define BACK_SWIPE_DISTANCE 80

static lv_obj_t *aion_root;
static lv_obj_t *elapsed_label;
static lv_obj_t *status_label;
static lv_obj_t *start_label;
static lv_timer_t *refresh_timer;
static bool running;
static uint32_t stored_elapsed_ms;
static uint32_t started_at_tick;
static int16_t gesture_start_x;
static int16_t gesture_start_y;
static bool returning_to_list;

static void draw_icon_rect(lv_draw_ctx_t *ctx, const lv_area_t *area,
                           uint32_t color, lv_coord_t radius) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius;
    lv_draw_rect(ctx, &dsc, area);
}

static void aion_icon_draw_event(lv_event_t *event) {
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);

    const lv_coord_t size = lv_area_get_width(&area);
    const lv_coord_t center_x = area.x1 + size / 2;
    const lv_coord_t center_y = area.y1 + size * 5 / 8;
    const lv_coord_t radius = size * 3 / 8;

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = lv_color_hex(COLOR_ACCENT);
    ring.width = 3;
    ring.rounded = true;
    lv_point_t center = {.x = center_x, .y = center_y};
    lv_draw_arc(ctx, &ring, &center, radius, 0, 359);

    const lv_area_t crown = {
        .x1 = center_x - size / 8, .y1 = area.y1 + 1,
        .x2 = center_x + size / 8, .y2 = area.y1 + size / 8,
    };
    draw_icon_rect(ctx, &crown, COLOR_ACCENT, size / 16);

    const lv_area_t stem = {
        .x1 = center_x - size / 16, .y1 = area.y1 + size / 8,
        .x2 = center_x + size / 16, .y2 = area.y1 + size / 5,
    };
    draw_icon_rect(ctx, &stem, COLOR_TEXT_DIM, 0);

    const lv_area_t side_button = {
        .x1 = area.x2 - size / 10, .y1 = center_y - size / 5,
        .x2 = area.x2, .y2 = center_y,
    };
    draw_icon_rect(ctx, &side_button, COLOR_TEXT_DIM, size / 16);

    lv_draw_line_dsc_t hand;
    lv_draw_line_dsc_init(&hand);
    hand.color = lv_color_hex(COLOR_TEXT);
    hand.width = 2;
    hand.round_start = true;
    hand.round_end = true;
    const lv_point_t hand_start = {.x = center_x, .y = center_y};
    const lv_point_t hand_end = {.x = center_x + size / 8,
                                 .y = center_y - size / 6};
    lv_draw_line(ctx, &hand, &hand_start, &hand_end);

    const lv_area_t pivot = {
        .x1 = center_x - size / 14, .y1 = center_y - size / 14,
        .x2 = center_x + size / 14, .y2 = center_y + size / 14,
    };
    draw_icon_rect(ctx, &pivot, COLOR_TEXT, LV_RADIUS_CIRCLE);
}

static void create_aion_icon(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, aion_icon_draw_event, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_invalidate(parent);
}

static uint32_t elapsed_ms(void) {
    return running ? stored_elapsed_ms + lv_tick_elaps(started_at_tick)
                   : stored_elapsed_ms;
}

static void update_display(void) {
    if (elapsed_label == NULL) return;

    const uint32_t total_deciseconds = elapsed_ms() / 100;
    const uint32_t minutes = total_deciseconds / 600;
    const uint32_t seconds = (total_deciseconds / 10) % 60;
    const uint32_t deciseconds = total_deciseconds % 10;
    lv_label_set_text_fmt(elapsed_label, "%02lu:%02lu.%lu",
                          (unsigned long)minutes, (unsigned long)seconds,
                          (unsigned long)deciseconds);
    lv_label_set_text(status_label, running ? "EM CURSO" :
                      stored_elapsed_ms == 0 ? "PRONTO" : "PAUSADO");
    lv_label_set_text(start_label, running ? "PAUSAR" : "INICIAR");
}

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    update_display();
}

static void style_round_button(lv_obj_t *button, lv_color_t background,
                               lv_color_t border) {
    lv_obj_set_size(button, 110, 110);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, background, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, border, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

static void start_pause_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    chronvs_system_ui_notify_activity();
    if (running) {
        stored_elapsed_ms = elapsed_ms();
        running = false;
    }
    else {
        started_at_tick = lv_tick_get();
        running = true;
    }
    update_display();
}

static void reset_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    chronvs_system_ui_notify_activity();
    stored_elapsed_ms = 0;
    if (running) started_at_tick = lv_tick_get();
    update_display();
}

static void aion_touch_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        gesture_start_x = point.x;
        gesture_start_y = point.y;
        returning_to_list = false;
        chronvs_system_ui_notify_activity();
    }
    else if (code == LV_EVENT_PRESSING && !returning_to_list) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        const int16_t dx = point.x - gesture_start_x;
        const int16_t dy = point.y - gesture_start_y;
        const int16_t vertical = dy < 0 ? -dy : dy;
        if (dx > BACK_SWIPE_DISTANCE && dx > vertical + 20) {
            returning_to_list = true;
            chronvs_app_open("apps");
        }
    }
}

static void show_aion(void) {
    update_display();
    lv_timer_resume(refresh_timer);
}

static void hide_aion(void) {
    lv_timer_pause(refresh_timer);
}

static lv_obj_t *create_aion(lv_obj_t *parent) {
    aion_root = lv_obj_create(parent);
    lv_obj_remove_style_all(aion_root);
    lv_obj_set_size(aion_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(aion_root, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(aion_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(aion_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(aion_root, aion_touch_event, LV_EVENT_ALL, NULL);

    lv_obj_t *title = lv_label_create(aion_root);
    lv_label_set_text(title, "AION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    status_label = lv_label_create(aion_root);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 68);

    elapsed_label = lv_label_create(aion_root);
    lv_obj_set_style_text_font(elapsed_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(elapsed_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(elapsed_label, LV_ALIGN_CENTER, 0, -35);

    lv_obj_t *start_button = lv_btn_create(aion_root);
    style_round_button(start_button, lv_color_hex(COLOR_PANEL_EDGE),
                       lv_color_hex(COLOR_TEXT_DIM));
    lv_obj_align(start_button, LV_ALIGN_CENTER, -65, 85);
    lv_obj_add_flag(start_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(start_button, start_pause_event, LV_EVENT_CLICKED, NULL);
    start_label = lv_label_create(start_button);
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(start_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(start_label);

    lv_obj_t *reset_button = lv_btn_create(aion_root);
    style_round_button(reset_button, lv_color_hex(COLOR_PANEL),
                       lv_color_hex(COLOR_PANEL_EDGE));
    lv_obj_align(reset_button, LV_ALIGN_CENTER, 65, 85);
    lv_obj_add_flag(reset_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(reset_button, reset_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = lv_label_create(reset_button);
    lv_label_set_text(reset_label, "ZERAR");
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(reset_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(reset_label);

    refresh_timer = lv_timer_create(refresh_timer_cb, 100, NULL);
    update_display();
    return aion_root;
}

const chronvs_app_t chronvs_aion_app = {
    .id = "aion",
    .name = "Aion",
    .create_icon = create_aion_icon,
    .launcher_visible = true,
    .create = create_aion,
    .on_show = show_aion,
    .on_hide = hide_aion,
};

CHRONVS_REGISTER_APP(chronvs_aion_app)
