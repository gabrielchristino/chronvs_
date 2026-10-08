#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"

#include "apps/watch_app.h"
#include "apps/app_catalog.h"
#include "platform/display_profile.h"
#include "services/Relogio_service.h"
#include "core/app_manager.h"
#include "ui/system_ui.h"
#include "src/draw/sw/lv_draw_sw.h"
#if !defined(CHRONVS_WATCH_RINGS_REFERENCE) && !defined(CHRONVS_WATCH_FLAT_BACKGROUND)
#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 1
#error "Regenerate the watch ring cache before changing its RGB565 format"
#endif
#include "apps/watch_ring_cache.h"
#ifdef CHRONVS_WATCH_MOTHER_CACHE
#include "apps/watch_mother_cache.h"
#endif
static lv_color_t *ring_cache_line;
#endif

/* PlatformIO's debug mode overrides per-source CMake -O2 with -Og.
 * Optimize only this translation unit, without fast-math or driver changes.
 * The baseline switch is used by the host pixel comparison. */
#if defined(__GNUC__) && !defined(CHRONVS_TEST_WATCH_BASELINE)
#pragma GCC optimize ("O2")
#endif

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
static lv_timer_t *animation_timer;
static float ambient_temperature_c = 24.0f;

/* Geometry only, never pixels. Reused across partial-buffer draw calls. */
static struct {
    bool valid;
    uint8_t day;
    float cx, cy;
    lv_point_t dates[31];
    lv_point_t minutes[12];
} chapter;

static const char date_text[31][3] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
    "12", "13", "14", "15", "16", "17", "18", "19", "20", "21",
    "22", "23", "24", "25", "26", "27", "28", "29", "30", "31",
};
static const char minute_text[12][3] = {
    "0", "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55",
};

typedef struct {
    lv_area_t bounds;
    lv_area_t parent_bounds;
} circle_cover_t;
static circle_cover_t circle_covers[2];
static unsigned circle_cover_count;

/* Only omit pixels strictly inside an opaque circle. Keep two pixels of
 * safety for integer rounding and antialiasing along the panel edge. */
static bool covered_by_circle(const lv_area_t *area, const lv_area_t *clip) {
    if (!circle_cover_count) return false;
    lv_area_t part;
    if (!_lv_area_intersect(&part, area, clip)) return false;
    for (unsigned i = 0; i < circle_cover_count; ++i) {
        const circle_cover_t *cover = &circle_covers[i];
        if (part.x1 < cover->parent_bounds.x1 || part.x2 > cover->parent_bounds.x2 ||
            part.y1 < cover->parent_bounds.y1 || part.y2 > cover->parent_bounds.y2) continue;
        /* Doubled coordinates avoid floating point and half-pixel centers. */
        const int cx = cover->bounds.x1 + cover->bounds.x2;
        const int cy = cover->bounds.y1 + cover->bounds.y2;
        const int radius = cover->bounds.x2 - cover->bounds.x1 - 4;
        const int dx = LV_MAX(LV_ABS(2 * part.x1 - cx), LV_ABS(2 * part.x2 - cx));
        const int dy = LV_MAX(LV_ABS(2 * part.y1 - cy), LV_ABS(2 * part.y2 - cy));
        if (radius > 0 && dx * dx + dy * dy < radius * radius) return true;
    }
    return false;
}

/* Trim opaque, rectangular siblings entering from an edge. LVGL's normal
 * cover test only skips this custom drawing when a whole buffer is covered.
 * Walking up also finds the quick panel above the app content layer. */
static bool visible_clock_clip(lv_obj_t *object, lv_area_t *clip) {
    circle_cover_count = 0;
    for (lv_obj_t *node = object; lv_obj_get_parent(node) != NULL;
         node = lv_obj_get_parent(node)) {
        lv_obj_t *parent = lv_obj_get_parent(node);
        const uint32_t count = lv_obj_get_child_cnt(parent);
        for (uint32_t i = lv_obj_get_index(node) + 1; i < count; ++i) {
            lv_obj_t *cover = lv_obj_get_child(parent, i);
            if (lv_obj_has_flag(cover, LV_OBJ_FLAG_HIDDEN) ||
                lv_obj_get_style_opa(cover, 0) != LV_OPA_COVER ||
                lv_obj_get_style_bg_opa(cover, 0) != LV_OPA_COVER ||
                lv_obj_get_style_opa_layered(cover, 0) != LV_OPA_COVER ||
                lv_obj_get_style_transform_angle(cover, 0) != 0 ||
                lv_obj_get_style_transform_zoom(cover, 0) != 256) continue;
            const lv_coord_t radius = lv_obj_get_style_radius(cover, 0);
            if (radius != 0) {
                if (radius == LV_RADIUS_CIRCLE &&
                    lv_area_get_width(&cover->coords) == lv_area_get_height(&cover->coords) &&
                    lv_obj_get_style_radius(parent, 0) == 0 &&
                    lv_obj_get_style_opa(parent, 0) == LV_OPA_COVER &&
                    lv_obj_get_style_opa_layered(parent, 0) == LV_OPA_COVER &&
                    circle_cover_count < 2) {
                    circle_covers[circle_cover_count++] = (circle_cover_t){
                        .bounds = cover->coords, .parent_bounds = parent->coords};
                }
                continue;
            }
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

#ifndef CHRONVS_WATCH_FLAT_BACKGROUND
static void update_chapter_geometry(float cx, float cy, uint8_t day) {
    const bool moved = !chapter.valid || chapter.cx != cx || chapter.cy != cy;
    if (moved) {
        for (int marker = 0; marker < 12; ++marker) {
            chapter.minutes[marker] = polar_point(cx, cy, 168, marker * 30.0f);
        }
    }
    if (moved || chapter.day != day) {
        const float date_step = 360.0f / 31.0f;
        const float date_rotation = 180.0f - (day - 1) * date_step;
        for (int date = 1; date <= 31; ++date) {
            const float angle = (date - 1) * date_step + date_rotation;
            chapter.dates[date - 1] = polar_point(cx, cy, 195, angle);
        }
    }
    chapter.cx = cx;
    chapter.cy = cy;
    chapter.day = day;
    chapter.valid = true;
}
#endif

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
    if (covered_by_circle(&area, ctx->clip_area)) return;
    lv_draw_rect(ctx, &dsc, &area);
}

static void draw_line(lv_draw_ctx_t *ctx, lv_point_t start, lv_point_t end,
                      uint32_t color, int width, bool rounded) {
    /* Conservative bounds include stroke width, round caps and antialiasing. */
    const int margin = width + 2;
    const lv_area_t *clip = ctx->clip_area;
    if ((start.x < clip->x1 - margin && end.x < clip->x1 - margin) ||
        (start.x > clip->x2 + margin && end.x > clip->x2 + margin) ||
        (start.y < clip->y1 - margin && end.y < clip->y1 - margin) ||
        (start.y > clip->y2 + margin && end.y > clip->y2 + margin)) return;
    const lv_area_t bounds = {LV_MIN(start.x,end.x)-margin, LV_MIN(start.y,end.y)-margin,
                             LV_MAX(start.x,end.x)+margin, LV_MAX(start.y,end.y)+margin};
    if (covered_by_circle(&bounds, clip)) return;
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
    const lv_coord_t height = lv_font_get_line_height(font);
    lv_area_t area = {
        .x1 = (lv_coord_t)lroundf(cx - width / 2.0f),
        .y1 = (lv_coord_t)lroundf(cy - height / 2.0f),
        .x2 = (lv_coord_t)lroundf(cx + width / 2.0f),
        .y2 = (lv_coord_t)lroundf(cy + height / 2.0f),
    };
    /* Same label bounds checked by LVGL, before descriptor initialization. */
    lv_area_t intersection;
    if (!_lv_area_intersect(&intersection, &area, ctx->clip_area)) return;
    if (covered_by_circle(&area, ctx->clip_area)) return;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = lv_color_hex(color);
    dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(ctx, &dsc, &area, text, NULL);
}

static int normalize_lv_arc_angle(float watch_angle) {
    int result = (int)lroundf(watch_angle - 90.0f) % 360;
    return result < 0 ? result + 360 : result;
}

static void draw_arc(lv_draw_ctx_t *ctx, float cx, float cy, int radius,
                     float start_watch_angle, float end_watch_angle,
                     uint32_t color, int width) {
    if (circle_cover_count) {
        const lv_area_t bounds = {(lv_coord_t)floorf(cx-radius-width-2),
            (lv_coord_t)floorf(cy-radius-width-2), (lv_coord_t)ceilf(cx+radius+width+2),
            (lv_coord_t)ceilf(cy+radius+width+2)};
        if (covered_by_circle(&bounds, ctx->clip_area)) return;
    }
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

static void draw_case_rings(lv_draw_ctx_t *ctx, float cx, float cy) {

#if !defined(CHRONVS_WATCH_RINGS_REFERENCE) && !defined(CHRONVS_WATCH_FLAT_BACKGROUND)
    /* Exact reference pixels for the stationary 412px case only. A row-sized
     * scratch allocation comes from the existing PSRAM LVGL pool, never DMA.
     * Moving the face or applying an external mask keeps the vector path. */
    if (ring_cache_line && cx == 205.5f && cy == 205.5f &&
        ctx->draw_rect == lv_draw_sw_rect && !lv_draw_mask_is_any(ctx->clip_area)) {
        const int first_y = LV_MAX(0, ctx->clip_area->y1);
        const int last_y = LV_MIN(411, ctx->clip_area->y2);
        for (int y = first_y; y <= last_y; ++y) {
            lv_area_t row = {0, y, 411, y};
            if (covered_by_circle(&row, ctx->clip_area)) continue;
            unsigned x = 0;
            for (unsigned run = watch_ring_offsets[y]; run < watch_ring_offsets[y+1]; ++run) {
                lv_color_t color = {.full = watch_ring_runs[run][1]};
                const unsigned end = watch_ring_runs[run][0];
                while (x < end) ring_cache_line[x++] = color;
            }
            lv_draw_sw_blend_dsc_t blend = {0};
            blend.blend_area = &row;
            blend.src_buf = ring_cache_line;
            blend.opa = LV_OPA_COVER;
            lv_draw_sw_blend(ctx, &blend);
        }
        return;
    }
#endif

    /* Overscan hides the antialiased edge beyond the round panel aperture. */
    draw_circle(ctx, cx, cy, 206, COLOR_DATE_RING, COLOR_TRACK, 1);
    draw_circle(ctx, cx, cy, 183, COLOR_FACE_DARK, COLOR_TRACK, 2);
}

static void draw_case_dates(lv_draw_ctx_t *ctx) {
    /* Independent date ring: today's number always meets the marker at 6. */
    for (int date = 1; date <= 31; ++date) {
        lv_point_t p = chapter.dates[date - 1];
        draw_text(ctx, p.x, p.y, date_text[date - 1], &lv_font_montserrat_12, COLOR_INK_DIM, 24);
    }

}

/* Drawn after the mother disk so its type can never be erased by that disk. */
static void draw_minute_chapter(lv_draw_ctx_t *ctx) {

    for (int marker = 0; marker < 12; ++marker) {
        /* The 30-minute position is reserved for the fixed marker. */
        if (marker == 6) continue;
        lv_point_t p = chapter.minutes[marker];
        draw_text(ctx, p.x, p.y, minute_text[marker], &lv_font_montserrat_18,
                  COLOR_INK, 34);
    }
}

static void draw_mother_face(lv_draw_ctx_t *ctx, float cx, float cy) {
#if defined(CHRONVS_WATCH_MOTHER_CACHE) && !defined(CHRONVS_WATCH_RINGS_REFERENCE) && !defined(CHRONVS_WATCH_FLAT_BACKGROUND)
    /* The border was rendered over the fixed case background. Date labels
     * lie outside this disk. Only changed spans are opaque: rectangular
     * corners would erase dates. Keep vector drawing for other compositions. */
    if (ring_cache_line && cx == 205.5f && cy == 205.5f &&
        ctx->draw_rect == lv_draw_sw_rect && !lv_draw_mask_is_any(ctx->clip_area)) {
        const int first_y = LV_MAX(0, ctx->clip_area->y1);
        const int last_y = LV_MIN(411, ctx->clip_area->y2);
        for (int y = first_y; y <= last_y; ++y) {
            unsigned run = watch_mother_offsets[y], limit = watch_mother_offsets[y+1];
            if (run == limit) continue;
            unsigned start = watch_mother_starts[y];
            lv_area_t row = {start, y, watch_mother_runs[limit-1][0]-1, y};
            lv_area_t intersection;
            if (!_lv_area_intersect(&intersection, &row, ctx->clip_area) ||
                covered_by_circle(&row, ctx->clip_area)) continue;
            unsigned x = start;
            for (; run < limit; ++run) {
                lv_color_t color = {.full = watch_mother_runs[run][1]};
                while (x < watch_mother_runs[run][0]) ring_cache_line[x++] = color;
            }
            lv_draw_sw_blend_dsc_t blend = {0};
            blend.blend_area = &row;
            blend.src_buf = &ring_cache_line[start];
            blend.opa = LV_OPA_COVER;
            lv_draw_sw_blend(ctx, &blend);
        }
        return;
    }
#endif
    draw_circle(ctx, cx, cy, 154, COLOR_FACE, COLOR_TRACK, 1);
}

static void draw_mother_disk(lv_draw_ctx_t *ctx, float cx, float cy,
                             float minute_angle) {
    CHRONVS_WATCH_DETAIL_BEGIN();
    draw_mother_face(ctx, cx, cy);
    CHRONVS_WATCH_DETAIL_MARK(MOTHER_FACE);

    /* The dominant minute hand runs from the center to the disk edge. */
    draw_hand(ctx, cx, cy, 0, 149, minute_angle, COLOR_INK, 8);
    CHRONVS_WATCH_DETAIL_MARK(MOTHER_HAND);
    draw_circle(ctx, cx, cy, 4, COLOR_FACE_DARK, COLOR_INK_DIM, 1);
    CHRONVS_WATCH_DETAIL_MARK(MOTHER_CENTER);
}

static void draw_hour_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                           float hour_angle) {
    if (!dial_visible(ctx, cx, cy, 73)) return;
    CHRONVS_WATCH_DETAIL_BEGIN();
    static const char *numbers[] = {"", "1", "", "3", "", "5",
                                    "", "7", "", "9", "", "11"};
    draw_circle(ctx, cx, cy, 75, COLOR_FACE, COLOR_TRACK, 2);
    CHRONVS_WATCH_DETAIL_MARK(HOURS_FACE);

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

    CHRONVS_WATCH_DETAIL_MARK(HOURS_SCALE);

    draw_hand(ctx, cx, cy, 0, 46, hour_angle, COLOR_INK, 8);
    CHRONVS_WATCH_DETAIL_MARK(HOURS_HAND);
}

static void draw_weekday_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                              float weekday_angle, uint8_t weekday,
                              uint8_t hour) {
    if (!dial_visible(ctx, cx, cy, 48)) return;
    draw_circle(ctx, cx, cy, 48, COLOR_FACE, COLOR_TRACK, 2);

    for (int day = 0; day < 7; ++day) {
        const float center_angle = day * (360.0f / 7.0f);
        const uint32_t color = (day == weekday) ? COLOR_RED : COLOR_INK_DIM;
        draw_arc(ctx, cx, cy, 42, center_angle - 16.0f,
                 center_angle + 16.0f, color, 4);
    }

    draw_hand(ctx, cx, cy, 0, 28, weekday_angle, COLOR_INK, 4);
}

static void draw_temperature_dial(lv_draw_ctx_t *ctx, float cx, float cy,
                                  float temperature_c) {
    if (!dial_visible(ctx, cx, cy, 48)) return;
    draw_circle(ctx, cx, cy, 48, COLOR_FACE, COLOR_TRACK, 2);

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
    CHRONVS_WATCH_PROFILE_BEGIN();
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_obj_t *object = lv_event_get_target(event);
    const lv_area_t *original_clip = ctx->clip_area;
    lv_area_t visible_clip = *original_clip;
    if (!visible_clock_clip(object, &visible_clip)) {
        CHRONVS_WATCH_PROFILE_MARK(SETUP);
        return;
    }
    ctx->clip_area = &visible_clip;
    const lv_area_t *coords = &object->coords;
    const float cx = (coords->x1 + coords->x2) * 0.5f;
    const float cy = (coords->y1 + coords->y2) * 0.5f;
#ifndef CHRONVS_WATCH_FLAT_BACKGROUND
    update_chapter_geometry(cx, cy, displayed_time.day);
#endif
    CHRONVS_WATCH_PROFILE_MARK(SETUP);

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
    CHRONVS_WATCH_PROFILE_MARK(BACKGROUND);

#ifdef CHRONVS_WATCH_FLAT_BACKGROUND
    /* A/B diagnostic only: keep normal invalidation, clipping and transfers. */
    ctx->clip_area = original_clip;
    return;
#endif

    const float seconds = displayed_time.second;
    const float minutes = displayed_time.minute + seconds / 60.0f;
    const float hours = (displayed_time.hour % 12) + minutes / 60.0f;

    const float minute_angle = fmodf(minutes * 6.0f, 360.0f);
    const float hour_angle = fmodf(hours * 30.0f, 360.0f);
    const float weekday_angle = displayed_time.weekday * (360.0f / 7.0f) +
                                hours * (360.0f / (7.0f * 24.0f));

    CHRONVS_WATCH_PROFILE_MARK(GEOMETRY);
    draw_case_rings(ctx, cx, cy);
    CHRONVS_WATCH_PROFILE_MARK(RINGS);
    draw_case_dates(ctx);
    CHRONVS_WATCH_PROFILE_MARK(DATES);
    draw_mother_disk(ctx, cx, cy, minute_angle);
    CHRONVS_WATCH_PROFILE_MARK(MOTHER);
    draw_minute_chapter(ctx);
    CHRONVS_WATCH_PROFILE_MARK(MINUTES);

    /* Fixed offsets preserve the former zero-minute composition without
     * orbital trigonometry. Only instrument hands move. The long minute hand
     * is drawn first so it passes behind these faces, leaving its tip visible. */
    static const point_f_t hour_offset = {0.0f, 66.0f};
    static const point_f_t weekday_offset = {-89.27080f, -32.49191f};
    static const point_f_t temperature_offset = {89.27080f, -32.49191f};
    CHRONVS_WATCH_PROFILE_MARK(GEOMETRY);

    draw_hour_dial(ctx, cx + hour_offset.x, cy + hour_offset.y, hour_angle);
    CHRONVS_WATCH_PROFILE_MARK(HOURS);
    draw_weekday_dial(ctx, cx + weekday_offset.x, cy + weekday_offset.y,
                      weekday_angle, displayed_time.weekday, displayed_time.hour);
    CHRONVS_WATCH_PROFILE_MARK(WEEKDAY);
    draw_temperature_dial(ctx, cx + temperature_offset.x, cy + temperature_offset.y,
                          ambient_temperature_c);
    CHRONVS_WATCH_PROFILE_MARK(TEMPERATURE);
    draw_date_marker(ctx, cx, cy);
    CHRONVS_WATCH_PROFILE_MARK(MARKER);
    ctx->clip_area = original_clip;
}

static void animation_timer_cb(lv_timer_t *timer) {
    (void)timer;
    chronvs_watch_app_refresh();
}

void chronvs_watch_app_refresh(void) {
    if (!clock_face) return;
    if (chronvs_system_ui_display_is_off() || !lv_obj_is_visible(clock_face)) return;
    chronvs_time_t time;
    if (chronvs_Relogio_time(&time)) chronvs_watch_app_set_time(&time);
    lv_obj_invalidate(clock_face);
}

static lv_obj_t *create_watch_app(lv_obj_t *parent) {
#if !defined(CHRONVS_WATCH_RINGS_REFERENCE) && !defined(CHRONVS_WATCH_FLAT_BACKGROUND)
    if (!ring_cache_line) ring_cache_line = lv_mem_alloc(DISPLAY_SIZE * sizeof(lv_color_t));
#endif
    clock_face = lv_obj_create(parent);
    lv_obj_remove_style_all(clock_face);
    lv_obj_set_size(clock_face, DISPLAY_SIZE, DISPLAY_SIZE);
    lv_obj_center(clock_face);
    lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(clock_face, clock_draw_event, LV_EVENT_DRAW_MAIN, NULL);

    animation_timer = lv_timer_create(animation_timer_cb, 1000, clock_face);
    lv_timer_pause(animation_timer);
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
    if (!time->valid) return; /* Keep the last snapshot instead of a blank face. */

    /* Keep one snapshot for every draw slice and incidental redraw. */
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
    /* Only explicit refresh requests invalidate the snapshot. */
}

static void show_watch_app(void) {
    lv_timer_pause(animation_timer);
    chronvs_watch_app_refresh();
}

static void hide_watch_app(void) {
    lv_timer_pause(animation_timer);
}

const chronvs_app_t chronvs_watch_app = {
    .id = "watch",
    .name = "Relogio",
    .create_icon = NULL,
    .launcher_visible = false,
    .create = create_watch_app,
    .on_show = show_watch_app,
    .on_hide = hide_watch_app,
};

CHRONVS_REGISTER_APP(chronvs_watch_app)
