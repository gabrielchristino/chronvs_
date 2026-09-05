#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"

#include "apps/watch_app.h"
#include "apps/app_catalog.h"
#include "core/app_manager.h"
#include "ui/system_ui.h"

#define DISPLAY_SIZE 412
#define PI_F 3.14159265358979323846f

/* Ressence-inspired palette, tuned for the SPD2010 IPS panel. */
#define COLOR_VOID       0x050706
#define COLOR_BEZEL      0x69716B
#define COLOR_BEZEL_DARK 0x1B211E
#define COLOR_DATE_RING  0x637461
#define COLOR_FACE       0x9EAD97
#define COLOR_FACE_DARK  0x91A18B
#define COLOR_INK        0xF2F2E9
#define COLOR_INK_DIM    0xD2D8CE
#define COLOR_TRACK      0x748173
#define COLOR_ORANGE     0xF2B84B
#define COLOR_RED        0xE47470
#define COLOR_BLUE       0x62A9D5

typedef struct {
    float x;
    float y;
} point_f_t;

static lv_obj_t *clock_face;

/* A useful fallback also makes the visual testable before the RTC is set. */
static chronvs_time_t displayed_time = {
    .second = 0, .minute = 0, .hour = 12, .day = 18,
    .weekday = 3, .month = 11, .year = 26, .valid = true,
};
static uint32_t displayed_time_tick;
static float ambient_temperature_c = 24.0f;
static uint32_t render_time_tick;

/* Trim opaque, rectangular siblings entering from an edge. LVGL's normal
 * cover test only skips this custom drawing when a whole buffer is covered.
 * Walking up also finds the quick panel above the app content layer. */
static bool visible_clock_clip(lv_obj_t *object, lv_area_t *clip) {
    for (lv_obj_t *node = object; lv_obj_get_parent(node) != NULL;
         node = lv_obj_get_parent(node)) {
        lv_obj_t *parent = lv_obj_get_parent(node);
        const uint32_t count = lv_obj_get_child_cnt(parent);
        for (uint32_t i = lv_obj_get_index(node) + 1; i < count; ++i) {
            lv_obj_t *cover = lv_obj_get_child(parent, i);
            if (lv_obj_has_flag(cover, LV_OBJ_FLAG_HIDDEN) ||
                lv_obj_get_style_opa(cover, 0) != LV_OPA_COVER ||
                lv_obj_get_style_bg_opa(cover, 0) != LV_OPA_COVER ||
                lv_obj_get_style_radius(cover, 0) != 0 ||
                lv_obj_get_style_transform_angle(cover, 0) != 0 ||
                lv_obj_get_style_transform_zoom(cover, 0) != 256) continue;
            lv_area_t area;
            if (!_lv_area_intersect(&area, &cover->coords, &parent->coords) ||
                !_lv_area_intersect(&area, &area, clip)) continue;
            if (area.x1 == clip->x1 && area.x2 == clip->x2) {
                if (area.y1 == clip->y1) clip->y1 = area.y2 + 1;
                else if (area.y2 == clip->y2) clip->y2 = area.y1 - 1;
            } else if (area.y1 == clip->y1 && area.y2 == clip->y2) {
                if (area.x1 == clip->x1) clip->x1 = area.x2 + 1;
                else if (area.x2 == clip->x2) clip->x2 = area.x1 - 1;
            }
            if (clip->x1 > clip->x2 || clip->y1 > clip->y2) return false;
        }
    }
    return true;
}

static bool dial_visible(lv_draw_ctx_t *ctx, float cx, float cy, float radius) {
    /* Include rounding and antialiasing beyond the nominal dial bounds. */
    radius += 2;
    return cx + radius >= ctx->clip_area->x1 &&
           cx - radius <= ctx->clip_area->x2 &&
           cy + radius >= ctx->clip_area->y1 &&
           cy - radius <= ctx->clip_area->y2;
}

static float clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float radians(float degrees) {
    return degrees * PI_F / 180.0f;
}

/* Angles in this file use watch convention: 0 at 12, clockwise positive. */
static lv_point_t polar_point(float cx, float cy, float radius, float angle_deg) {
    const float angle = radians(angle_deg);
    lv_point_t p = {
        .x = (lv_coord_t)lroundf(cx + sinf(angle) * radius),
        .y = (lv_coord_t)lroundf(cy - cosf(angle) * radius),
    };
    return p;
}

static point_f_t rotate_offset(point_f_t p, float angle_deg) {
    const float angle = radians(angle_deg);
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    point_f_t result = {
        .x = p.x * cosine - p.y * sine,
        .y = p.x * sine + p.y * cosine,
    };
    return result;
}

static point_f_t polar_offset(float radius, float angle_deg) {
    const float angle = radians(angle_deg);
    point_f_t result = {
        .x = sinf(angle) * radius,
        .y = -cosf(angle) * radius,
    };
    return result;
}

static void draw_circle(lv_draw_ctx_t *ctx, float cx, float cy, float radius,
                        uint32_t fill, uint32_t border, int border_width) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(fill);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_color = lv_color_hex(border);
    dsc.border_opa = border_width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP;
    dsc.border_width = border_width;

    lv_area_t area = {
        .x1 = (lv_coord_t)lroundf(cx - radius),
        .y1 = (lv_coord_t)lroundf(cy - radius),
        .x2 = (lv_coord_t)lroundf(cx + radius),
        .y2 = (lv_coord_t)lroundf(cy + radius),
    };
    lv_draw_rect(ctx, &dsc, &area);
}

static void draw_line(lv_draw_ctx_t *ctx, lv_point_t start, lv_point_t end,
                      uint32_t color, int width, bool rounded) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = rounded;
    dsc.round_end = rounded;
    lv_draw_line(ctx, &dsc, &start, &end);
}

static void draw_radial_line(lv_draw_ctx_t *ctx, float cx, float cy,
                             float inner_radius, float outer_radius,
                             float angle, uint32_t color, int width) {
    draw_line(ctx, polar_point(cx, cy, inner_radius, angle),
              polar_point(cx, cy, outer_radius, angle), color, width, true);
}

static void draw_text(lv_draw_ctx_t *ctx, float cx, float cy, const char *text,
                      const lv_font_t *font, uint32_t color, int width) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = lv_color_hex(color);
    dsc.align = LV_TEXT_ALIGN_CENTER;

    const lv_coord_t height = lv_font_get_line_height(font);
    lv_area_t area = {
        .x1 = (lv_coord_t)lroundf(cx - width / 2.0f),
        .y1 = (lv_coord_t)lroundf(cy - height / 2.0f),
        .x2 = (lv_coord_t)lroundf(cx + width / 2.0f),
        .y2 = (lv_coord_t)lroundf(cy + height / 2.0f),
    };
    lv_draw_label(ctx, &dsc, &area, text, NULL);
}

static int normalize_lv_arc_angle(float watch_angle) {
    int result = (int)lroundf(watch_angle - 90.0f) % 360;
    return result < 0 ? result + 360 : result;
}

static void draw_arc(lv_draw_ctx_t *ctx, float cx, float cy, int radius,
                     float start_watch_angle, float end_watch_angle,
                     uint32_t color, int width) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = width;
    dsc.rounded = true;
    lv_point_t center = {.x = (lv_coord_t)cx, .y = (lv_coord_t)cy};
    lv_draw_arc(ctx, &dsc, &center, radius,
                normalize_lv_arc_angle(start_watch_angle),
                normalize_lv_arc_angle(end_watch_angle));
}

static void draw_hand(lv_draw_ctx_t *ctx, float cx, float cy, float tail,
                      float length, float angle, uint32_t color, int width) {
    draw_line(ctx, polar_point(cx, cy, -tail, angle),
              polar_point(cx, cy, length, angle), color, width, true);
}

static void draw_fixed_case(lv_draw_ctx_t *ctx, float cx, float cy, uint8_t day) {
    char text[4];

    /* Overscan hides the antialiased edge beyond the round panel aperture. */
    draw_circle(ctx, cx, cy, 206, COLOR_DATE_RING, COLOR_TRACK, 1);
    draw_circle(ctx, cx, cy, 183, COLOR_FACE_DARK, COLOR_TRACK, 2);

    /* Independent date ring: today's number always meets the marker at 6. */
    const float date_step = 360.0f / 31.0f;
    const float date_rotation = 180.0f - (day - 1) * date_step;
    for (int date = 1; date <= 31; ++date) {
        const float angle = (date - 1) * date_step + date_rotation;
        snprintf(text, sizeof(text), "%d", date);
        lv_point_t p = polar_point(cx, cy, 195, angle);
        draw_text(ctx, p.x, p.y, text, &lv_font_montserrat_12, COLOR_INK_DIM, 24);
    }

}

/* Drawn after the mother disk so its type can never be erased by that disk. */
static void draw_minute_chapter(lv_draw_ctx_t *ctx, float cx, float cy) {
    char text[4];

    for (int minute = 0; minute < 60; ++minute) {
        /* The 30-minute position is reserved for the fixed marker. */
        if (minute == 30) continue;

        const float angle = minute * 6.0f;
        if (minute % 5 == 0) {
            snprintf(text, sizeof(text), "%d", minute);
            lv_point_t p = polar_point(cx, cy, 168, angle);
            draw_text(ctx, p.x, p.y, text, &lv_font_montserrat_18,
                      COLOR_INK, 34);
        } else {
            draw_radial_line(ctx, cx, cy, 160, 172, angle,
                             COLOR_INK, 2);
        }
    }
}

static void draw_mother_disk(lv_draw_ctx_t *ctx, float cx, float cy,
                             float minute_angle) {
    draw_circle(ctx, cx, cy, 154, COLOR_FACE, COLOR_TRACK, 1);

    /* The dominant minute hand runs from the center to the disk edge. */
    draw_hand(ctx, cx, cy, 0, 149, minute_angle, COLOR_INK, 8);
    draw_circle(ctx, cx, cy, 4, COLOR_FACE_DARK, COLOR_INK_DIM, 1);
}

static void draw_hour_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                           float hour_angle) {
    if (!dial_visible(ctx, cx, cy, 73)) return;
    static const char *numbers[] = {"", "1", "", "3", "", "5",
                                    "", "7", "", "9", "", "11"};
    draw_circle(ctx, cx, cy, 75, COLOR_FACE, COLOR_TRACK, 2);

    for (int hour = 0; hour < 12; ++hour) {
        if (numbers[hour][0] != '\0') {
            lv_point_t p = polar_point(cx, cy, 63, hour * 30.0f);
            draw_text(ctx, p.x, p.y, numbers[hour], &lv_font_montserrat_18,
                      COLOR_INK, 28);
        }
        else if (hour != 0) {
            /* Even hours use bars; odd hours use numerals, like the original. */
            draw_radial_line(ctx, cx, cy, 60, 70, hour * 30.0f,
                             COLOR_INK_DIM, 4);
        }
        else {
            lv_point_t p = polar_point(cx, cy, 63, hour * 30.0f);
            draw_text(ctx, p.x, p.y, "P", &lv_font_montserrat_18,
                      COLOR_INK_DIM, 28);
        }
    }

    draw_circle(ctx, cx, cy, 55, COLOR_FACE, COLOR_TRACK, 2);

    draw_hand(ctx, cx, cy, 0, 46, hour_angle, COLOR_INK, 8);
}

static void draw_weekday_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                              float weekday_angle, uint8_t weekday,
                              uint8_t hour) {
    if (!dial_visible(ctx, cx, cy, 48)) return;
    draw_circle(ctx, cx, cy, 48, COLOR_FACE, COLOR_TRACK, 2);
    draw_circle(ctx, cx, cy, 33, COLOR_FACE, COLOR_TRACK, 2);

    for (int day = 0; day < 7; ++day) {
        const float center_angle = day * (360.0f / 7.0f);
        const uint32_t color = (day == weekday) ? COLOR_RED : COLOR_INK_DIM;
        draw_arc(ctx, cx, cy, 42, center_angle - 16.0f,
                 center_angle + 16.0f, color, 4);
    }

    draw_hand(ctx, cx, cy, 0, 28, weekday_angle, COLOR_INK, 4);
}

static void draw_seconds_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                              float second_angle) {
    if (!dial_visible(ctx, cx, cy, 18)) return;
    draw_circle(ctx, cx, cy, 18, COLOR_FACE, COLOR_TRACK, 2);
    for (int marker = 0; marker < 12; ++marker) {
        draw_radial_line(ctx, cx, cy, 20, 23, marker * 30.0f,
                         marker == 6 ? COLOR_RED : COLOR_TRACK, 2);
    }
    draw_hand(ctx, cx, cy, 0, 15, second_angle, COLOR_INK, 3);
    lv_point_t tip = polar_point(cx, cy, 10, second_angle - 180);
    draw_circle(ctx, tip.x, tip.y, 1, COLOR_RED, COLOR_RED, 0);
}

static void draw_temperature_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                                  float temperature_c) {
    if (!dial_visible(ctx, cx, cy, 48)) return;
    draw_circle(ctx, cx, cy, 48, COLOR_FACE, COLOR_TRACK, 2);
    draw_circle(ctx, cx, cy, 33, COLOR_FACE, COLOR_TRACK, 2);

    for (int day = 0; day < 5; ++day) {
        const float center_angle = day * (360.0f / 5.0f);
        uint32_t color = day == 2 ? COLOR_BLUE : day == 3 ? COLOR_ORANGE : COLOR_INK_DIM;
        draw_arc(ctx, cx, cy, 42, center_angle - 30.0f,
                 center_angle + 30.0f, color, 4);
    }

    // draw_arc(ctx, cx, cy, 42, 200, 275, COLOR_BLUE, 4);
    // draw_arc(ctx, cx, cy, 42, 85, 160, COLOR_ORANGE, 4);

    const float normalized = (clampf(temperature_c, -20.0f, 60.0f) + 20.0f) / 80.0f;
    const float needle_angle = -120.0f + normalized * 240.0f;
    draw_hand(ctx, cx, cy, 0, 28, needle_angle, COLOR_INK, 4);
}

static void draw_date_marker(lv_draw_ctx_t *ctx, float cx, float cy) {
    lv_point_t triangle[3] = {
        {.x = (lv_coord_t)cx,       .y = (lv_coord_t)(cy + 178)},
        {.x = (lv_coord_t)(cx - 9), .y = (lv_coord_t)(cy + 162)},
        {.x = (lv_coord_t)(cx + 9), .y = (lv_coord_t)(cy + 162)},
    };
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(COLOR_ORANGE);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 1;
    dsc.border_color = lv_color_hex(COLOR_INK_DIM);
    lv_draw_polygon(ctx, &dsc, triangle, 3);

    draw_circle(ctx, cx - 1, cy + 168, 3, COLOR_BEZEL_DARK, COLOR_BEZEL_DARK, 0);
}

static void clock_draw_event(lv_event_t *event) {
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *object = lv_event_get_target(event);
    const lv_area_t *original_clip = ctx->clip_area;
    lv_area_t visible_clip = *original_clip;
    if (!visible_clock_clip(object, &visible_clip)) return;
    ctx->clip_area = &visible_clip;
    const lv_area_t *coords = &object->coords;
    const float cx = (coords->x1 + coords->x2) * 0.5f;
    const float cy = (coords->y1 + coords->y2) * 0.5f;

    /*
     * The custom object has no normal LVGL background. Clear every pixel in
     * its 412 x 412 area so no stale LCD-GRAM data can survive around the dial.
     */
    lv_draw_rect_dsc_t background_dsc;
    lv_draw_rect_dsc_init(&background_dsc);
    background_dsc.bg_color = lv_color_hex(COLOR_BEZEL_DARK);
    background_dsc.bg_opa = LV_OPA_COVER;
    background_dsc.border_opa = LV_OPA_TRANSP;
    lv_draw_rect(ctx, &background_dsc, coords);

    const uint32_t elapsed_ms = render_time_tick - displayed_time_tick;
    const float elapsed_seconds = elapsed_ms / 1000.0f;
    const float seconds = displayed_time.second + elapsed_seconds;
    const float minutes = displayed_time.minute + seconds / 60.0f;
    const float hours = (displayed_time.hour % 12) + minutes / 60.0f;

    const float minute_angle = fmodf(minutes * 6.0f, 360.0f);
    const float hour_angle = fmodf(hours * 30.0f, 360.0f);
    const float second_angle = fmodf(seconds * 6.0f, 360.0f);
    const float weekday_angle = displayed_time.weekday * (360.0f / 7.0f) +
                                hours * (360.0f / (7.0f * 24.0f));

    draw_fixed_case(ctx, cx, cy, displayed_time.day);
    draw_mother_disk(ctx, cx, cy, minute_angle);
    draw_minute_chapter(ctx, cx, cy);

    /*
     * Local mother-disk coordinates are rotated for orbital translation.
     * Faces remain upright globally: visually this is the exact -minute_angle
     * counter-rotation that keeps their typography gyroscopically aligned.
     */
    /*
     * Layout in the mother disk's reference frame:
     * hours are exactly opposite the minute hand; the remaining instruments
     * reproduce the top / lower-right / bottom composition of the reference.
     */
    const point_f_t hour_local = polar_offset(66.0f, 180.0f);
    const point_f_t weekday_local = polar_offset(95.0f, 290.0f);
    const point_f_t temperature_local = polar_offset(95.0f, 70.0f);
    const point_f_t seconds_local = polar_offset(120.0f, 113.0f);

    const point_f_t hour_orbit = rotate_offset(hour_local, minute_angle);
    const point_f_t weekday_orbit = rotate_offset(weekday_local, minute_angle);
    const point_f_t seconds_orbit = rotate_offset(seconds_local, minute_angle);
    const point_f_t temperature_orbit = rotate_offset(temperature_local, minute_angle);

    draw_hour_dial(ctx, cx + hour_orbit.x, cy + hour_orbit.y, hour_angle);
    draw_weekday_dial(ctx, cx + weekday_orbit.x, cy + weekday_orbit.y,
                      weekday_angle, displayed_time.weekday, displayed_time.hour);
    draw_temperature_dial(ctx, cx + temperature_orbit.x, cy + temperature_orbit.y,
                          ambient_temperature_c);
    draw_seconds_dial(ctx, cx + seconds_orbit.x, cy + seconds_orbit.y, second_angle);
    draw_date_marker(ctx, cx, cy);
    ctx->clip_area = original_clip;
}

static void animation_timer_cb(lv_timer_t *timer) {
    render_time_tick = lv_tick_get();
    lv_obj_invalidate((lv_obj_t *)timer->user_data);
}

static lv_obj_t *create_watch_app(lv_obj_t *parent) {
    clock_face = lv_obj_create(parent);
    lv_obj_remove_style_all(clock_face);
    lv_obj_set_size(clock_face, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_center(clock_face);
    lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(clock_face, clock_draw_event, LV_EVENT_DRAW_MAIN, NULL);

    displayed_time_tick = lv_tick_get();
    render_time_tick = displayed_time_tick;
    lv_timer_t *animation_timer = lv_timer_create(animation_timer_cb, 1000, clock_face);
    chronvs_system_ui_init(clock_face, animation_timer);
    return clock_face;
}

/* Public hook for the future ambient-temperature sensor. */
void chronvs_watch_app_set_ambient_temperature(float temperature_c) {
    ambient_temperature_c = clampf(temperature_c, -20.0f, 60.0f);
    if (clock_face != NULL && !chronvs_system_ui_display_is_off()) {
        lv_obj_invalidate(clock_face);
    }
}

void chronvs_watch_app_set_time(const chronvs_time_t *time) {
    if (!time->valid) return; /* Keep the animated fallback instead of a blank face. */

    /* Keep the interpolation origin stable when the RTC returns the same
     * second, including after an early refresh caused by waking the screen. */
    if (time->second == displayed_time.second &&
        time->minute == displayed_time.minute &&
        time->hour == displayed_time.hour &&
        time->day == displayed_time.day &&
        time->weekday == displayed_time.weekday &&
        time->month == displayed_time.month &&
        time->year == displayed_time.year) {
        return;
    }

    displayed_time = *time;
    displayed_time_tick = lv_tick_get();
    render_time_tick = displayed_time_tick;
    lv_obj_invalidate(clock_face);
}

static void show_watch_app(void) {
    render_time_tick = lv_tick_get();
    if (clock_face != NULL) lv_obj_invalidate(clock_face);
}

const chronvs_app_t chronvs_watch_app = {
    .id = "watch",
    .name = "Relogio",
    .create_icon = NULL,
    .launcher_visible = false,
    .create = create_watch_app,
    .on_show = show_watch_app,
    .on_hide = NULL,
};

CHRONVS_REGISTER_APP(chronvs_watch_app)
