#include "apps/app_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include "core/calendar.h"
#include "services/rtc_service.h"
#include "ui/aion_widgets.h"
#include "ui/app_input.h"
#include "ui/mnemo_font.h"
#include "ui/system_ui.h"

enum { CELL_W = 42, CELL_H = 29, GRID_X = 59, GRID_Y = 154 };
static const char *months[] = {"Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho",
    "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"};
static const char *weekdays[] = {"Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira",
    "Quinta-feira", "Sexta-feira", "Sábado"};
static lv_obj_t *month_page, *detail_page, *grid, *month_label, *year_label;
static lv_obj_t *today_button, *unavailable;
static lv_obj_t *detail_day, *detail_month, *detail_weekday, *distance_label;
static lv_timer_t *refresh;
static chronvs_time_t today;
static int year, month, selected_day;
static bool visible, detail, was_off, dirty, reset_month;
static uint32_t read_tick;
static void render(void);
static void poll(lv_timer_t *timer);
static void back(void) {
    if (detail) { detail = false; render(); }
    else chronvs_app_open("apps");
}
static void change_month(int direction) {
    if (detail || chronvs_system_ui_display_is_off()) return;
    if (chronvs_calendar_step(&year, &month, direction)) render();
}
static chronvs_ui_app_input_t input = {.back = back, .vertical = change_month};

static void hidden(lv_obj_t *obj, bool hide) {
    if (hide) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void disabled(lv_obj_t *obj, bool disable) {
    if (disable) lv_obj_add_state(obj, LV_STATE_DISABLED);
    else lv_obj_clear_state(obj, LV_STATE_DISABLED);
}

static void draw_grid(lv_event_t *event) {
    if (!year) return;
    lv_area_t bounds;
    lv_obj_get_coords(grid, &bounds);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    int first = chronvs_calendar_weekday(year, month, 1);
    int count = chronvs_calendar_days(year, month);
    for (int day = 1; day <= count; ++day) {
        int slot = first + day - 1;
        int x = bounds.x1 + (slot % 7) * CELL_W;
        int y = bounds.y1 + (slot / 7) * CELL_H;
        bool current = today.valid && year == 2000 + today.year && month == today.month && day == today.day;
        if (current) {
            lv_draw_rect_dsc_t circle;
            lv_draw_rect_dsc_init(&circle);
            circle.bg_color = lv_color_hex(CHRONVS_UI_ACCENT);
            circle.radius = LV_RADIUS_CIRCLE;
            lv_area_t area = {x + 7, y, x + 34, y + 27};
            lv_draw_rect(ctx, &circle, &area);
        }
        char text[12]; snprintf(text, sizeof(text), "%d", day);
        lv_draw_label_dsc_t label;
        lv_draw_label_dsc_init(&label);
        label.font = &lv_font_montserrat_18;
        label.align = LV_TEXT_ALIGN_CENTER;
        label.color = lv_color_hex(current ? CHRONVS_UI_PANEL : CHRONVS_UI_TEXT);
        lv_area_t area = {x, y + 3, x + CELL_W - 1, y + CELL_H - 1};
        lv_draw_label(ctx, &label, &area, text, NULL);
    }
}

static void select_date(lv_event_t *event) {
    (void)event;
    if (input.consumed || chronvs_system_ui_display_is_off() || !year) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t point; lv_indev_get_point(indev, &point);
    /* Small drags must not select a different date on release. */
    if (abs(point.x - input.start.x) > 12 || abs(point.y - input.start.y) > 12) return;
    lv_area_t bounds; lv_obj_get_coords(grid, &bounds);
    int x = point.x - bounds.x1, y = point.y - bounds.y1;
    if (x < 0 || x >= 7 * CELL_W || y < 0 || y >= 6 * CELL_H) return;
    int day = y / CELL_H * 7 + x / CELL_W - chronvs_calendar_weekday(year, month, 1) + 1;
    if (!chronvs_calendar_valid(year, month, day)) return;
    selected_day = day;
    detail = true;
    render();
}

static void go_today(lv_event_t *event) {
    (void)event;
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) {
        lv_point_t point; lv_indev_get_point(indev, &point);
        if (abs(point.x - input.start.x) > 12 || abs(point.y - input.start.y) > 12) return;
    }
    dirty = reset_month = true;
    poll(refresh);
}

static void render(void) {
    hidden(month_page, detail);
    hidden(detail_page, !detail);
    if (detail) {
        char text[64];
        snprintf(text, sizeof(text), "%02d", selected_day);
        lv_label_set_text(detail_day, text);
        snprintf(text, sizeof(text), "%s de %d", months[month - 1], year);
        lv_label_set_text(detail_month, text);
        lv_label_set_text(detail_weekday, weekdays[chronvs_calendar_weekday(year, month, selected_day)]);
        if (!today.valid) snprintf(text, sizeof(text), "Data atual indisponível");
        else {
            int delta = chronvs_calendar_ordinal(year, month, selected_day) -
                chronvs_calendar_ordinal(2000 + today.year, today.month, today.day);
            if (!delta) snprintf(text, sizeof(text), "Hoje");
            else if (delta == 1) snprintf(text, sizeof(text), "Amanhã");
            else if (delta == -1) snprintf(text, sizeof(text), "Ontem");
            else if (delta > 0) snprintf(text, sizeof(text), "Daqui a %d dias", delta);
            else snprintf(text, sizeof(text), "Há %d dias", -delta);
        }
        lv_label_set_text(distance_label, text);
        return;
    }
    hidden(grid, !year);
    hidden(unavailable, today.valid);
    disabled(today_button, !today.valid);
    if (year) {
        char text[12]; snprintf(text, sizeof(text), "%d", year);
        lv_label_set_text(month_label, months[month - 1]);
        lv_label_set_text(year_label, text);
    } else {
        lv_label_set_text(month_label, "Sem data");
        lv_label_set_text(year_label, "--");
    }
    lv_obj_invalidate(grid);
}

static void poll(lv_timer_t *timer) {
    (void)timer;
    if (!visible) return;
    if (chronvs_system_ui_display_is_off()) { was_off = true; return; }
    if (!dirty && !was_off && lv_tick_elaps(read_tick) < 60000) return;
    chronvs_time_t now = chronvs_rtc_read();
    now.valid = now.valid && chronvs_calendar_valid(2000 + now.year, now.month, now.day);
    bool changed = now.valid != today.valid || now.year != today.year ||
        now.month != today.month || now.day != today.day;
    today = now;
    if (today.valid && (reset_month || !year)) {
        year = 2000 + today.year; month = today.month;
        reset_month = false;
        changed = true;
    }
    if (changed || dirty) render();
    dirty = was_off = false;
    read_tick = lv_tick_get();
}

static void draw_icon(lv_event_t *event) {
    lv_area_t a; lv_obj_get_coords(lv_event_get_target(event), &a);
    int x = a.x1 + (lv_area_get_width(&a) - 34) / 2;
    int y = a.y1 + (lv_area_get_height(&a) - 36) / 2;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    /* Bound page with a folded corner and one large date, never a key grid. */
    static const lv_point_t outline[] = {
        {0, 5}, {33, 5}, {33, 27}, {25, 35}, {0, 35}, {0, 5},
    };
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(CHRONVS_UI_ACCENT); line.width = 2;
    line.round_start = line.round_end = true;
    for (unsigned i = 1; i < sizeof(outline) / sizeof(outline[0]); ++i) {
        lv_point_t p = {x + outline[i-1].x, y + outline[i-1].y};
        lv_point_t q = {x + outline[i].x, y + outline[i].y};
        lv_draw_line(ctx, &line, &p, &q);
    }
    lv_point_t p = {x, y + 12}, q = {x + 33, y + 12};
    lv_draw_line(ctx, &line, &p, &q);
    p = (lv_point_t){x + 25, y + 35}; q = (lv_point_t){x + 25, y + 27};
    lv_draw_line(ctx, &line, &p, &q);
    p = (lv_point_t){x + 33, y + 27};
    lv_draw_line(ctx, &line, &q, &p);
    line.color = lv_color_hex(CHRONVS_UI_TEXT);
    for (int col = 0; col < 2; ++col) {
        p = (lv_point_t){x + 8 + col * 17, y};
        q = (lv_point_t){p.x, y + 7};
        lv_draw_line(ctx, &line, &p, &q);
    }
    lv_draw_label_dsc_t date; lv_draw_label_dsc_init(&date);
    date.font = &lv_font_montserrat_18;
    date.color = lv_color_hex(CHRONVS_UI_TEXT);
    date.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t number = {x + 2, y + 13, x + 28, y + 34};
    lv_draw_label(ctx, &date, &number, "31", NULL);
}

static void create_icon(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, draw_icon, LV_EVENT_DRAW_MAIN, NULL);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int y, int width, const lv_font_t *font) {
    lv_obj_t *obj = chronvs_aion_label(parent, text, y, font);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    return obj;
}

static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent); chronvs_aion_surface(root);
    month_page = lv_obj_create(root); chronvs_aion_surface(month_page);
    detail_page = lv_obj_create(root); chronvs_aion_surface(detail_page);
    month_label = label(month_page, "", 68, 150, &chronvs_mnemo_font);
    year_label = label(month_page, "", 95, 100, &lv_font_montserrat_18);
    static const char *short_days[] = {"D", "S", "T", "Q", "Q", "S", "S"};
    for (int i = 0; i < 7; ++i) {
        lv_obj_t *day = label(month_page, short_days[i], 132, CELL_W, &lv_font_montserrat_12);
        lv_obj_set_align(day, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(day, GRID_X + i * CELL_W, 132);
        lv_obj_set_style_text_color(day, lv_color_hex(CHRONVS_UI_TEXT_DIM), 0);
    }
    grid = lv_obj_create(month_page);
    lv_obj_remove_style_all(grid);
    lv_obj_set_pos(grid, GRID_X, GRID_Y);
    lv_obj_set_size(grid, 7 * CELL_W, 6 * CELL_H);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(grid, draw_grid, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(grid, select_date, LV_EVENT_CLICKED, NULL);
    today_button = chronvs_aion_action(month_page, "Hoje", 0, 342, CHRONVS_UI_ACTION_WIDTH, false, go_today, 0);
    unavailable = label(month_page, "RTC indisponivel", 328, 220, &lv_font_montserrat_12);
    detail_day = label(detail_page, "", 115, 200, &lv_font_montserrat_48);
    detail_month = label(detail_page, "", 184, 290, &chronvs_mnemo_font);
    detail_weekday = label(detail_page, "", 223, 290, &chronvs_mnemo_font);
    distance_label = label(detail_page, "", 281, 290, &chronvs_mnemo_font);
    lv_obj_set_style_text_color(distance_label, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    chronvs_ui_app_input_bind(root, &input);
    refresh = lv_timer_create(poll, 250, NULL);
    lv_timer_pause(refresh);
    hidden(detail_page, true);
    return root;
}

static void show(void) {
    visible = dirty = reset_month = true;
    detail = false;
    input.consumed = false;
    lv_timer_resume(refresh);
    poll(refresh);
}

static void hide(void) {
    visible = false;
    lv_timer_pause(refresh);
}

const chronvs_app_t chronvs_hemera_app = {
    .id = "hemera", .name = "Hemera", .launcher_visible = true,
    .create_icon = create_icon, .create = create, .on_show = show, .on_hide = hide,
};
CHRONVS_REGISTER_APP(chronvs_hemera_app)
