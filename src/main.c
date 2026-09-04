#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "Display_SPD2010.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"

static const char *TAG = "chronvs";

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
    uint8_t second, minute, hour, day, weekday, month, year;
    bool valid;
} clock_time_t;

typedef struct {
    float x;
    float y;
} point_f_t;

static lv_obj_t *clock_face;

/* A useful fallback also makes the visual testable before the RTC is set. */
static clock_time_t displayed_time = {
    .second = 36, .minute = 9, .hour = 10, .day = 18,
    .weekday = 5, .month = 9, .year = 26, .valid = true,
};
static uint32_t displayed_time_tick;
static float ambient_temperature_c = 24.0f;

static uint8_t bcd_to_decimal(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

static void scan_onboard_i2c(void) {
    static const uint8_t addresses[] = {0x20, 0x51, 0x53, 0x6A, 0x6B};
    static const char *names[] = {
        "TCA9554", "PCF85063 RTC", "SPD2010 touch", "QMI8658 IMU", "QMI8658 IMU"
    };

    for (size_t i = 0; i < sizeof(addresses); ++i) {
        esp_err_t result = i2c_master_write_to_device(
            I2C_MASTER_NUM, addresses[i], NULL, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "I2C 0x%02X %-14s %s", addresses[i], names[i],
                 result == ESP_OK ? "OK" : "no response");
    }
}

static clock_time_t read_rtc(void) {
    const uint8_t register_start = 0x04; /* PCF85063: seconds through year */
    uint8_t raw[7] = {0};
    clock_time_t time = {0};

    if (i2c_master_write_read_device(I2C_MASTER_NUM, 0x51, &register_start, 1,
                                     raw, sizeof(raw), pdMS_TO_TICKS(100)) != ESP_OK) {
        return time;
    }

    time.second = bcd_to_decimal(raw[0] & 0x7F);
    time.minute = bcd_to_decimal(raw[1] & 0x7F);
    time.hour = bcd_to_decimal(raw[2] & 0x3F);
    time.day = bcd_to_decimal(raw[3] & 0x3F);
    time.weekday = raw[4] & 0x07;
    time.month = bcd_to_decimal(raw[5] & 0x1F);
    time.year = bcd_to_decimal(raw[6]);
    time.valid = time.second < 60 && time.minute < 60 && time.hour < 24 &&
                 time.day >= 1 && time.day <= 31 && time.weekday < 7 &&
                 time.month >= 1 && time.month <= 12;
    return time;
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
    draw_circle(ctx, cx, cy, 209, COLOR_BEZEL_DARK, COLOR_BEZEL_DARK, 0);
    draw_circle(ctx, cx, cy, 201, COLOR_BEZEL_DARK, COLOR_BEZEL_DARK, 0);
    draw_circle(ctx, cx, cy, 197, COLOR_DATE_RING, COLOR_TRACK, 1);
    draw_circle(ctx, cx, cy, 174, COLOR_FACE_DARK, COLOR_TRACK, 2);

    /* Independent date ring: today's number always meets the marker at 6. */
    const float date_step = 360.0f / 31.0f;
    const float date_rotation = 180.0f - (day - 1) * date_step;
    for (int date = 1; date <= 31; ++date) {
        const float angle = (date - 1) * date_step + date_rotation;
        snprintf(text, sizeof(text), "%d", date);
        lv_point_t p = polar_point(cx, cy, 186, angle);
        draw_text(ctx, p.x, p.y, text, &lv_font_montserrat_12,
                  date == day ? COLOR_VOID : COLOR_INK_DIM, 24);
    }

}

/* Drawn after the mother disk so its type can never be erased by that disk. */
static void draw_minute_chapter(lv_draw_ctx_t *ctx, float cx, float cy) {
    char text[4];

    for (int minute = 0; minute < 60; ++minute) {
        /* 5/15/25/... replace their radial mark instead of sitting on it. */
        if (minute % 10 == 5) continue;
        const float angle = minute * 6.0f;
        const int major = minute % 5 == 0;
        draw_radial_line(ctx, cx, cy, major ? 163 : 167, 171, angle,
                         COLOR_INK, major ? 3 : 2);
    }
    for (int minute = 5; minute < 60; minute += 10) {
        snprintf(text, sizeof(text), "%d", minute);
        lv_point_t p = polar_point(cx, cy, 156, minute * 6.0f);
        draw_text(ctx, p.x, p.y, text, &lv_font_montserrat_18,
                  COLOR_INK, 34);
    }
}

static void draw_mother_disk(lv_draw_ctx_t *ctx, float cx, float cy,
                             float minute_angle) {
    draw_circle(ctx, cx, cy, 146, COLOR_FACE, COLOR_TRACK, 1);

    /* The dominant minute hand runs from the center to the disk edge. */
    draw_hand(ctx, cx, cy, 0, 141, minute_angle, COLOR_INK, 8);
    draw_circle(ctx, cx, cy, 4, COLOR_FACE_DARK, COLOR_INK_DIM, 1);
}

static void draw_hour_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                           float hour_angle) {
    static const char *numbers[] = {"", "1", "", "3", "", "5",
                                    "", "7", "", "9", "", "11"};
    draw_circle(ctx, cx, cy, 73, COLOR_FACE, COLOR_TRACK, 2);

    for (int hour = 0; hour < 12; ++hour) {
        if (numbers[hour][0] != '\0') {
            lv_point_t p = polar_point(cx, cy, 52, hour * 30.0f);
            draw_text(ctx, p.x, p.y, numbers[hour], &lv_font_montserrat_18,
                      COLOR_INK, 28);
        }
        else if (hour != 0) {
            /* Even hours use bars; odd hours use numerals, like the original. */
            draw_radial_line(ctx, cx, cy, 60, 67, hour * 30.0f,
                             COLOR_INK_DIM, 4);
        }
    }

    /* Three horizontal strokes stand in for the Ressence hand logo at 12. */
    for (int i = 0; i < 3; ++i) {
        lv_point_t a = {.x = (lv_coord_t)(cx - 8 + i * 2),
                        .y = (lv_coord_t)(cy - 50 + i * 4)};
        lv_point_t b = {.x = (lv_coord_t)(cx + 8 - i * 2), .y = a.y};
        draw_line(ctx, a, b, COLOR_INK_DIM, 2, true);
    }

    draw_hand(ctx, cx, cy, 10, 46, hour_angle, COLOR_INK, 8);
    draw_circle(ctx, cx, cy, 4, COLOR_FACE_DARK, COLOR_INK, 1);
}

static void draw_weekday_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                              float weekday_angle, uint8_t weekday,
                              uint8_t hour) {
    draw_circle(ctx, cx, cy, 42, COLOR_FACE, COLOR_TRACK, 2);

    for (int day = 0; day < 7; ++day) {
        const float center_angle = day * (360.0f / 7.0f);
        const uint32_t color = day == weekday ? COLOR_RED : COLOR_INK_DIM;
        draw_arc(ctx, cx, cy, 33, center_angle - 16.0f,
                 center_angle + 16.0f, color, day == weekday ? 6 : 4);
    }

    draw_hand(ctx, cx, cy, 4, 25, weekday_angle, COLOR_INK, 4);
    draw_circle(ctx, cx, cy, 3, COLOR_FACE_DARK, COLOR_INK, 1);
    draw_text(ctx, cx, cy + 15, hour < 12 ? "AM" : "PM",
              &lv_font_montserrat_12, COLOR_TRACK, 28);
}

static void draw_seconds_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                              float second_angle) {
    draw_circle(ctx, cx, cy, 25, COLOR_FACE, COLOR_TRACK, 2);
    for (int marker = 0; marker < 12; ++marker) {
        draw_radial_line(ctx, cx, cy, 20, 23, marker * 30.0f,
                         marker == 6 ? COLOR_RED : COLOR_TRACK, 2);
    }
    draw_hand(ctx, cx, cy, 5, 19, second_angle, COLOR_INK, 3);
    lv_point_t tip = polar_point(cx, cy, 19, second_angle);
    draw_circle(ctx, tip.x, tip.y, 2, COLOR_ORANGE, COLOR_ORANGE, 0);
    draw_circle(ctx, cx, cy, 3, COLOR_FACE_DARK, COLOR_INK, 1);
}

static void draw_temperature_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                                  float temperature_c) {
    draw_circle(ctx, cx, cy, 44, COLOR_FACE, COLOR_TRACK, 2);
    draw_circle(ctx, cx, cy, 32, COLOR_FACE_DARK, COLOR_TRACK, 1);
    draw_arc(ctx, cx, cy, 37, 200, 275, COLOR_BLUE, 6);
    draw_arc(ctx, cx, cy, 37, 85, 160, COLOR_ORANGE, 6);

    const float normalized = (clampf(temperature_c, -20.0f, 60.0f) + 20.0f) / 80.0f;
    const float needle_angle = -120.0f + normalized * 240.0f;
    draw_hand(ctx, cx, cy, 5, 27, needle_angle, COLOR_INK, 5);
    draw_circle(ctx, cx, cy, 3, COLOR_FACE_DARK, COLOR_INK, 1);
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

    draw_circle(ctx, cx, cy + 168, 3, COLOR_BEZEL_DARK, COLOR_BEZEL_DARK, 0);
}

static void clock_draw_event(lv_event_t *event) {
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *object = lv_event_get_target(event);
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

    const uint32_t elapsed_ms = lv_tick_elaps(displayed_time_tick);
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
    const point_f_t hour_local = polar_offset(70.0f, 180.0f);
    const point_f_t weekday_local = polar_offset(96.0f, 305.0f);
    const point_f_t temperature_local = polar_offset(96.0f, 70.0f);
    const point_f_t seconds_local = polar_offset(112.0f, 110.0f);

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
}

static void animation_timer_cb(lv_timer_t *timer) {
    lv_obj_invalidate((lv_obj_t *)timer->user_data);
}

static void create_clock_screen(void) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_VOID), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    clock_face = lv_obj_create(screen);
    lv_obj_remove_style_all(clock_face);
    lv_obj_set_size(clock_face, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_center(clock_face);
    lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(clock_face, clock_draw_event, LV_EVENT_DRAW_MAIN, NULL);

    displayed_time_tick = lv_tick_get();
    lv_timer_create(animation_timer_cb, 100, clock_face);
}

/* Public hook for the future ambient-temperature sensor. */
void chronvs_set_ambient_temperature(float temperature_c) {
    ambient_temperature_c = clampf(temperature_c, -20.0f, 60.0f);
    if (clock_face != NULL) lv_obj_invalidate(clock_face);
}

static void update_clock_screen(const clock_time_t *time) {
    if (!time->valid) return; /* Keep the animated fallback instead of a blank face. */
    displayed_time = *time;
    displayed_time_tick = lv_tick_get();
    lv_obj_invalidate(clock_face);
}

void app_main(void) {
    ESP_LOGI(TAG, "Chronvs orbital LVGL clock starting");
    I2C_Init();
    EXIO_Init();
    scan_onboard_i2c();
    LCD_Init();
    LVGL_Init();
    create_clock_screen();

    TickType_t next_rtc_update = 0;
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if (now >= next_rtc_update) {
            const clock_time_t time = read_rtc();
            update_clock_screen(&time);
            next_rtc_update = now + pdMS_TO_TICKS(250);
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
