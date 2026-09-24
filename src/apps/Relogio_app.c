#include "apps/app_catalog.h"

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "core/app_manager.h"
#include "lvgl.h"
#include "ui/system_ui.h"
#include "apps/Relogio_pages.h"
#include "ui/Relogio_widgets.h"
#include "services/Relogio_service.h"

#define COLOR_PANEL       0x26302B
#define COLOR_PANEL_EDGE  0x748173
#define COLOR_TEXT        0xF2F2E9
#define COLOR_TEXT_DIM    0xB7C0B5
#define COLOR_ACCENT      0xF2B84B
#define SCREEN_SIZE 412
#define BACK_SWIPE_DISTANCE 80

static lv_obj_t *Relogio_root;
static lv_obj_t *stopwatch_page;
static unsigned current_page;
static uint32_t last_stopwatch_refresh;
static lv_obj_t *elapsed_label;
static lv_obj_t *status_label;
static lv_obj_t *start_label;
static lv_timer_t *refresh_timer;
static uint64_t shown_deciseconds = UINT64_MAX;
static int shown_stopwatch_state = -1;
static int16_t gesture_start_x;
static int16_t gesture_start_y;
static bool returning_to_list;
static bool page_back_allowed;
static bool page_vertical_allowed;

static void draw_icon_rect(lv_draw_ctx_t *ctx, const lv_area_t *area,
                           uint32_t color, lv_coord_t radius) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius;
    lv_draw_rect(ctx, &dsc, area);
}

static void Relogio_icon_draw_event(lv_event_t *event) {
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

static void create_Relogio_icon(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, Relogio_icon_draw_event, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_invalidate(parent);
}

static void update_display(void) {
    if (elapsed_label == NULL) return;

    const uint64_t elapsed = chronvs_stopwatch_elapsed_ms();
    const uint64_t deciseconds = elapsed / 100;
    const int state = chronvs_stopwatch_running() ? 2 : elapsed ? 1 : 0;
    if (deciseconds != shown_deciseconds) {
        lv_label_set_text_fmt(elapsed_label, "%02" PRIu64 ":%02u.%u",
                             deciseconds / 600, (unsigned)(deciseconds / 10 % 60),
                             (unsigned)(deciseconds % 10));
        shown_deciseconds = deciseconds;
    }
    if (state != shown_stopwatch_state) {
        lv_label_set_text(status_label, state == 2 ? "EM CURSO" :
                          state == 0 ? "PRONTO" : "PAUSADO");
        lv_label_set_text(start_label, state == 2 ? "Pausar" : "Iniciar");
        shown_stopwatch_state = state;
    }
}

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (chronvs_system_ui_display_is_off()) return;
    chronvs_Relogio_pages_refresh();
    if (!current_page && lv_tick_elaps(last_stopwatch_refresh) >= 100) {
        last_stopwatch_refresh = lv_tick_get();
        update_display();
    }
}

static void select_page(unsigned page) {
    current_page = page;
    if (page) lv_obj_add_flag(stopwatch_page, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(stopwatch_page, LV_OBJ_FLAG_HIDDEN);
    chronvs_Relogio_pages_show(page);
}

static void start_pause_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    chronvs_system_ui_notify_activity();
    if (chronvs_stopwatch_running()) chronvs_stopwatch_pause();
    else chronvs_stopwatch_start();
    update_display();
}

static void reset_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    chronvs_system_ui_notify_activity();
    chronvs_stopwatch_reset();
    update_display();
}

static void Relogio_touch_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        gesture_start_x = point.x;
        gesture_start_y = point.y;
        returning_to_list = false;
        page_back_allowed = chronvs_Relogio_pages_can_swipe_back(lv_event_get_target(event));
        page_vertical_allowed = chronvs_Relogio_pages_can_swipe_vertical(lv_event_get_target(event));
        chronvs_system_ui_notify_activity();
    }
    else if (code == LV_EVENT_PRESSING && !returning_to_list) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        const int16_t dx = point.x - gesture_start_x;
        const int16_t dy = point.y - gesture_start_y;
        const int16_t vertical = dy < 0 ? -dy : dy;
        if (dx > BACK_SWIPE_DISTANCE && dx > vertical + 20) {
            returning_to_list = true;
            lv_indev_wait_release(lv_indev_get_act());
            if (!chronvs_Relogio_pages_back()) chronvs_app_open("apps");
        }
        else if (vertical > 80 && vertical > (dx < 0 ? -dx : dx) + 20) {
            bool navigated = false;
            if (chronvs_Relogio_pages_editing()) {
                if (page_vertical_allowed)
                    navigated = chronvs_Relogio_pages_vertical(dy < 0 ? 1 : -1);
            } else if ((dy < 0 && current_page < 2) ||
                       (dy > 0 && current_page > 0 && page_back_allowed)) {
                select_page(dy < 0 ? current_page + 1 : current_page - 1);
                navigated = true;
            }
            if (navigated) {
                returning_to_list = true;
                lv_indev_wait_release(lv_indev_get_act());
            }
        }
    }
}

static void show_Relogio(void) {
    select_page(0);
    update_display();
    lv_timer_resume(refresh_timer);
}

static void hide_Relogio(void) {
    lv_timer_pause(refresh_timer);
}

static lv_obj_t *create_Relogio(lv_obj_t *parent) {
    shown_deciseconds = UINT64_MAX;
    shown_stopwatch_state = -1;
    Relogio_root = lv_obj_create(parent);
    lv_obj_remove_style_all(Relogio_root);
    lv_obj_set_size(Relogio_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_style_bg_color(Relogio_root, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(Relogio_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(Relogio_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(Relogio_root, Relogio_touch_event, LV_EVENT_ALL, NULL);

    stopwatch_page = lv_obj_create(Relogio_root);
    chronvs_Relogio_surface(stopwatch_page);

    lv_obj_t *title = lv_label_create(stopwatch_page);
    lv_label_set_text(title, "Cronometro");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    status_label = lv_label_create(stopwatch_page);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 68);

    elapsed_label = lv_label_create(stopwatch_page);
    lv_obj_set_style_text_font(elapsed_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(elapsed_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(elapsed_label, LV_ALIGN_CENTER, 0, -35);

    lv_obj_t *start_button = chronvs_Relogio_action(stopwatch_page, "Iniciar", -66, 264,
                                                CHRONVS_UI_PAIR_WIDTH, false, start_pause_event, 0);
    start_label = lv_obj_get_child(start_button, 0);
    chronvs_Relogio_action(stopwatch_page, "Zerar", 66, 264, CHRONVS_UI_PAIR_WIDTH, true, reset_event, 0);

    chronvs_Relogio_pages_init(Relogio_root);
    refresh_timer = lv_timer_create(refresh_timer_cb, 20, NULL);
    update_display();
    return Relogio_root;
}

const chronvs_app_t chronvs_Relogio_app = {
    .id = "Relogio",
    .name = "Relogio",
    .create_icon = create_Relogio_icon,
    .launcher_visible = true,
    .create = create_Relogio,
    .on_show = show_Relogio,
    .on_hide = hide_Relogio,
};

CHRONVS_REGISTER_APP(chronvs_Relogio_app)
