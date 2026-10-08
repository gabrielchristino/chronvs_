#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CHRONVS_WATCH_SETUP, CHRONVS_WATCH_BACKGROUND, CHRONVS_WATCH_GEOMETRY,
    CHRONVS_WATCH_RINGS, CHRONVS_WATCH_DATES, CHRONVS_WATCH_MOTHER, CHRONVS_WATCH_MINUTES,
    CHRONVS_WATCH_HOURS, CHRONVS_WATCH_WEEKDAY, CHRONVS_WATCH_TEMPERATURE,
    CHRONVS_WATCH_SECONDS, CHRONVS_WATCH_MARKER,
    CHRONVS_WATCH_MOTHER_FACE, CHRONVS_WATCH_MOTHER_HAND, CHRONVS_WATCH_MOTHER_CENTER,
    CHRONVS_WATCH_HOURS_FACE, CHRONVS_WATCH_HOURS_SCALE,
    CHRONVS_WATCH_HOURS_INNER, CHRONVS_WATCH_HOURS_HAND, CHRONVS_WATCH_SECTION_COUNT
} chronvs_watch_section_t;

typedef enum { CHRONVS_PANEL_QUICK, CHRONVS_PANEL_LAUNCHER, CHRONVS_PANEL_COUNT } chronvs_panel_t;
#if defined(CHRONVS_DISPLAY_PROFILE) && defined(CHRONVS_PANEL_PROFILE)
#include "lvgl.h"
void chronvs_display_profile_bind_panel(lv_obj_t *root, chronvs_panel_t panel);
#define CHRONVS_PANEL_PROFILE_BIND(root, panel) chronvs_display_profile_bind_panel(root, CHRONVS_PANEL_##panel)
#else
#define CHRONVS_PANEL_PROFILE_BIND(root, panel) ((void)0)
#endif

#ifdef CHRONVS_DISPLAY_PROFILE
void chronvs_display_profile_init(void);
void chronvs_display_profile_poll(bool display_off);
int64_t chronvs_display_profile_watch_begin(void);
int64_t chronvs_display_profile_watch_detail_begin(void);
int64_t chronvs_display_profile_watch_mark(chronvs_watch_section_t section, int64_t start);
#define CHRONVS_WATCH_PROFILE_BEGIN() int64_t profile_start = chronvs_display_profile_watch_begin()
#define CHRONVS_WATCH_PROFILE_MARK(section) \
    (profile_start = chronvs_display_profile_watch_mark(CHRONVS_WATCH_##section, profile_start))
#else
#define CHRONVS_WATCH_PROFILE_BEGIN() ((void)0)
#define CHRONVS_WATCH_PROFILE_MARK(section) ((void)0)
static inline void chronvs_display_profile_init(void) {}
static inline void chronvs_display_profile_poll(bool display_off) { (void)display_off; }
#endif

#if defined(CHRONVS_DISPLAY_PROFILE) && defined(CHRONVS_WATCH_DETAIL_PROFILE)
#define CHRONVS_WATCH_DETAIL_BEGIN() int64_t profile_start = chronvs_display_profile_watch_detail_begin()
#define CHRONVS_WATCH_DETAIL_MARK(section) CHRONVS_WATCH_PROFILE_MARK(section)
#else
#define CHRONVS_WATCH_DETAIL_BEGIN() ((void)0)
#define CHRONVS_WATCH_DETAIL_MARK(section) ((void)0)
#endif
