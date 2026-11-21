/* Render the real watch, launcher and global controls with hardware stubs. */
#define main service_test_main
#define nvs_set_blob Relogio_test_set_blob
#include "Relogio_service_test.c"
#undef main
#undef nvs_set_blob
#include "lvgl.h"
#ifdef CHRONVS_BRIGHTNESS_ARC_REFERENCE
#define paint_brightness_get(obj) lv_arc_get_value(obj)
#define paint_brightness_set(obj,value) lv_arc_set_value(obj,value)
#elif defined(CHRONVS_BRIGHTNESS_SLIDER_REFERENCE)
#define paint_brightness_get(obj) lv_slider_get_value(obj)
#define paint_brightness_set(obj,value) lv_slider_set_value(obj,value,LV_ANIM_OFF)
#endif
#if defined(CHRONVS_BRIGHTNESS_ARC_REFERENCE) || defined(CHRONVS_BRIGHTNESS_SLIDER_REFERENCE)
#define TEST_CONTINUOUS_LIGHT
#define TEST_NORMAL_BRIGHTNESS 70
#else
#define TEST_NORMAL_BRIGHTNESS 50
#endif
#include "apps/app_catalog.h"
#include "apps/watch_app.h"
#include "ui/system_ui.h"
#include "ui/control_style.h"
#include "ui/circle_fill.h"
#include "esp_heap_caps.h"
#include "services/Notas_service.h"
#include "services/sound_service.h"
#include "services/weather_service.h"
#include "services/gps_service.h"
#include "services/agnss_service.h"
#include "services/run_service.h"
#include "platform/lvgl_memory.h"
#include "platform/display_bench.h"
#include "platform/panel_autotest.h"
#include "ui/Relogio_alert.h"
#include "ui/power_menu.h"
#include <stdlib.h>

/* Exercise diagnostic wrappers with the real LVGL input and display drivers. */
#ifndef CHRONVS_DISPLAY_PROFILE
#define CHRONVS_DISPLAY_PROFILE
#endif
#define CHRONVS_LVGL_OPT_LABEL "host"
#define pixels profile_pixels
#include "platform/display_profile.c"
#undef pixels

int nvs_set_blob(nvs_handle_t h, const char *k, const void *in, size_t size) {
    if (!strncmp(k,"note",4)) {
        assert(size==sizeof(Notas_note_t)); return fail_save ? -1 : 0;
    }
    return Relogio_test_set_blob(h,k,in,size);
}

static lv_indev_state_t contact;
static bool gps_requested;
static bool gps_recording_requested;
static bool gps_workout;
static unsigned assist_requests;
static chronvs_agnss_snapshot_t assisted={.message="Aguardando hora por NTP"};
bool chronvs_gps_assist_request(bool force) { (void)force; if(gps_workout)return false; ++assist_requests;return true; }
void chronvs_gps_workout_active(bool enabled) { gps_workout=enabled; }
bool chronvs_gps_workout_is_active(void) { return gps_workout; }
void chronvs_agnss_snapshot(chronvs_agnss_snapshot_t *out) { *out=assisted; }
bool chronvs_agnss_active(void) { return assisted.state==AGNSS_DOWNLOAD || assisted.state==AGNSS_SENDING; }
const char *chronvs_agnss_reference_name(void) { return "CEP 04686-001 / raio 5 km"; }
static chronvs_gps_snapshot_t gps_snapshot;
void chronvs_gps_request(bool enabled) { gps_requested = enabled; }
void chronvs_gps_recording_request(bool enabled) { gps_recording_requested = enabled; }
void chronvs_gps_poll(bool off) { gps_snapshot.running = gps_recording_requested || (gps_requested && !off); }
void chronvs_gps_snapshot(chronvs_gps_snapshot_t *out) { *out = gps_snapshot; }
#ifdef CHRONVS_GPS_STANDBY_TEST
static unsigned gps_queries, gps_tests;
bool chronvs_gps_query_version(void) {
    ++gps_queries; snprintf(gps_snapshot.reply, sizeof(gps_snapshot.reply), "SW=TEST-5N31"); return true;
}
bool chronvs_gps_test_standby(void) {
    ++gps_tests; gps_snapshot.test = GPS_TEST_OBSERVING; gps_snapshot.remaining_s = 60; return true;
}
#endif
static bool power_request;
static unsigned power_off_calls, restart_calls;
bool chronvs_board_take_power_menu_request(void) {
    bool requested = power_request; power_request = false; return requested;
}
void chronvs_board_power_off(void) { ++power_off_calls; }
void chronvs_board_restart(void) { ++restart_calls; }
static bool reminder_ringing;
void chronvs_sound_set_ringing(bool value) { reminder_ringing=value; }
static unsigned weather_queries, weather_rtc_reads, weather_cache_reads, weather_result_reads;
static chronvs_weather_snapshot_t saved_weather;
static chronvs_weather_state_t weather_state;
bool chronvs_weather_init(void) { return true; }
bool chronvs_weather_get_snapshot(chronvs_weather_snapshot_t *snapshot) {
    ++weather_cache_reads;
    *snapshot=saved_weather; return snapshot->valid;
}
chronvs_weather_state_t chronvs_weather_state(void) { return weather_state; }
const char *chronvs_weather_error(void) { return ""; }
bool chronvs_weather_request_update(void) {
    if (weather_state==CHRONVS_WEATHER_FETCHING) return false;
    ++weather_queries; weather_state=CHRONVS_WEATHER_FETCHING; return true;
}
bool chronvs_weather_take_result(chronvs_weather_snapshot_t *s,char *e,size_t n) {
    ++weather_result_reads;
    (void)s; (void)e; (void)n; return false;
}
chronvs_time_t chronvs_rtc_read(void) {
    assert(!chronvs_system_ui_display_is_off()); ++weather_rtc_reads;
    return (chronvs_time_t){.year=26,.month=9,.day=10,.hour=12,.valid=true};
}
static lv_point_t point;
static void read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver; data->state=contact; data->point=point;
}
static void elapse(unsigned ms) {
    while(ms) {
        unsigned step=ms>35?35:ms;
        lv_tick_inc(step); lv_timer_handler(); chronvs_panel_autotest_poll(); ms-=step;
    }
}
static void touch(int x, int y, lv_indev_state_t state) {
    point=(lv_point_t){x,y}; contact=state; elapse(35);
}
static void tap(int x,int y) {
    touch(x,y,LV_INDEV_STATE_PR); touch(x,y,LV_INDEV_STATE_REL);
}
static lv_obj_t *find_textarea(lv_obj_t *tree) {
    if(lv_obj_check_type(tree,&lv_textarea_class)) return tree;
    for(unsigned i=0;i<lv_obj_get_child_cnt(tree);++i) {
        lv_obj_t *found=find_textarea(lv_obj_get_child(tree,i));
        if(found) return found;
    }
    return NULL;
}
static lv_obj_t *find_label_text(lv_obj_t *tree, const char *text) {
    if (lv_obj_check_type(tree, &lv_label_class) && !strcmp(lv_label_get_text(tree), text)) return tree;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(tree); ++i) {
        lv_obj_t *found = find_label_text(lv_obj_get_child(tree, i), text);
        if (found) return found;
    }
    return NULL;
}

void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (size==16U*1024U && getenv("CHRONVS_TEST_BACKGROUND_CACHE_FAIL")) return NULL;
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
    if (size == 44U * 44U * sizeof(lv_color_t) &&
        getenv("CHRONVS_TEST_ICON_CACHE_FAIL") != NULL) return NULL;
#endif
    return calloc(count, size);
}
static unsigned lvgl_pool_allocations;
void *heap_caps_malloc(size_t size, unsigned caps) {
#ifdef CHRONVS_WATCH_SCENE_CACHE
    if (size == 96U*1024U) {
        assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return malloc(size);
    }
#endif
    assert(size==LV_MEM_SIZE && caps==(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++lvgl_pool_allocations;
    return malloc(size);
}

uint8_t LCD_Backlight;
void Set_Backlight(uint8_t brightness) { LCD_Backlight=brightness; }
int nvs_flash_init(void) { return 0; }
int nvs_flash_erase(void) { return 0; }
static uint8_t sound_volume = 1, saved_volume = 4;
static int saved_light_mode = -1;
static unsigned previews;
static uint8_t preview_volume;
void chronvs_sound_preview(void) { ++previews; preview_volume = sound_volume; }
uint8_t chronvs_sound_volume(void) { return sound_volume; }
void chronvs_sound_set_volume(uint8_t level) { assert(level <= 5); sound_volume = level; }
int nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) {
    (void)h;
    if (!strcmp(k, "volume")) { *v = saved_volume; return 0; }
    if (!strcmp(k, "light_mode")) {
        const char *mode=getenv("CHRONVS_TEST_LIGHT_MODE");
        if (saved_light_mode<0 && mode && *mode) saved_light_mode=atoi(mode);
        if (saved_light_mode>=0) { *v=(uint8_t)saved_light_mode; return 0; }
    }
    const char *legacy=!strcmp(k,"brightness") ? getenv("CHRONVS_TEST_LEGACY_BRIGHTNESS") :
                       !strcmp(k,"eco") ? getenv("CHRONVS_TEST_LEGACY_ECO") : NULL;
    if (legacy) { *v=(uint8_t)atoi(legacy); return 0; }
    return -1;
}
int nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) {
    (void)h;
    if (!strcmp(k, "volume")) saved_volume = v;
    if (!strcmp(k,"light_mode")) saved_light_mode=v;
    return 0;
}

static void test_power_menu(void) {
    /* Opening over an existing contact must not confirm an action on release. */
    touch(259,180,LV_INDEV_STATE_PR);
    power_request = true; chronvs_power_menu_poll();
    touch(259,180,LV_INDEV_STATE_REL); chronvs_power_menu_poll();
    assert(find_label_text(lv_layer_top(), "Energia"));
    assert(!power_off_calls && !restart_calls);
    tap(206,272); chronvs_power_menu_poll();
    assert(!find_label_text(lv_layer_top(), "Energia"));
    power_request = true; chronvs_power_menu_poll();
    touch(153,180,LV_INDEV_STATE_PR);
    touch(253,180,LV_INDEV_STATE_PR);
    touch(253,180,LV_INDEV_STATE_REL); chronvs_power_menu_poll();
    assert(!find_label_text(lv_layer_top(), "Energia"));
    assert(!power_off_calls && !restart_calls);
    power_request = true; chronvs_power_menu_poll();
    tap(153,180); chronvs_power_menu_poll();
    assert(restart_calls == 1 && !power_off_calls);
    power_request = true; chronvs_power_menu_poll();
    tap(259,180); chronvs_power_menu_poll();
    assert(power_off_calls == 1 && restart_calls == 1);
    /* AUTO sleep keeps the modal; the first touch only wakes it. */
    power_request = true; chronvs_power_menu_poll();
    elapse(130000);
    assert(chronvs_system_ui_display_is_off());
    tap(259,180); chronvs_power_menu_poll();
    assert(!chronvs_system_ui_display_is_off());
    assert(power_off_calls == 1 && restart_calls == 1);
    assert(find_label_text(lv_layer_top(), "Energia"));
    tap(206,272); chronvs_power_menu_poll();
    puts("Power menu: explicit actions, cancel, swipe and wake guard passed.");
}

static unsigned char pixels[412*412*3];
static unsigned rect_draws;
static uint64_t flushed_pixels;
void __real_lv_draw_rect(lv_draw_ctx_t *, const lv_draw_rect_dsc_t *, const lv_area_t *);
void __wrap_lv_draw_rect(lv_draw_ctx_t *ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *area) {
    ++rect_draws;
    __real_lv_draw_rect(ctx,dsc,area);
}
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    flushed_pixels += lv_area_get_size(area);
    for(int y=area->y1;y<=area->y2;++y) for(int x=area->x1;x<=area->x2;++x) {
        lv_color32_t c={.full=lv_color_to32(*colors++)};
        unsigned char *p=&pixels[3*((411-y)*412+x)];
        if((x-206)*(x-206)+(y-206)*(y-206)>206*206) p[0]=p[1]=p[2]=0;
        else {p[0]=c.ch.blue;p[1]=c.ch.green;p[2]=c.ch.red;}
    }
    lv_disp_flush_ready(driver);
}
static void save_current_pixels(const char *name) {
    char path[160];snprintf(path,sizeof(path),".pio/host-tests/%s.bmp",name);
    FILE *f=fopen(path,"wb");assert(f);
    unsigned char h[54]={'B','M'};
    uint32_t size=sizeof(pixels)+54,offset=54,dib=40,dim=412;uint16_t planes=1,bits=24;
    memcpy(h+2,&size,4);memcpy(h+10,&offset,4);memcpy(h+14,&dib,4);
    memcpy(h+18,&dim,4);memcpy(h+22,&dim,4);memcpy(h+26,&planes,2);memcpy(h+28,&bits,2);
    fwrite(h,1,54,f);fwrite(pixels,1,sizeof(pixels),f);fclose(f);
}
static void capture(const char *name) {
    lv_obj_update_layout(lv_scr_act()); lv_tick_inc(100);lv_timer_handler();lv_refr_now(NULL);
    save_current_pixels(name);
}
static int icon_peak_contrast(lv_obj_t *icon) {
    lv_area_t area; lv_obj_get_coords(icon,&area);
    lv_color32_t bg={.full=lv_color_to32(lv_color_hex(CHRONVS_UI_PANEL))};
    const int channels[3]={bg.ch.blue,bg.ch.green,bg.ch.red};
    int peak=0;
    for (int y=area.y1; y<=area.y2; ++y) for (int x=area.x1; x<=area.x2; ++x) {
        assert(x>=0 && x<412 && y>=0 && y<412);
        for (int c=0; c<3; ++c) {
            int delta=abs((int)pixels[3*((411-y)*412+x)+c]-channels[c]);
            if (delta>peak) peak=delta;
        }
    }
    return peak;
}
static unsigned refresh_count;
static unsigned transparent_label_draws;
static void count_transparent_label_draw(lv_event_t *event) {
    (void)event; ++transparent_label_draws;
}
static unsigned render_start_count;
static void count_render_start(lv_disp_drv_t *drv) { (void)drv; ++render_start_count; }
static void count_refresh(lv_disp_drv_t *drv,uint32_t ms,uint32_t px) {
    (void)drv; (void)ms; (void)px; ++refresh_count;
}

typedef struct { bool enabled; int16_t id; lv_draw_mask_radius_param_t mask; } cache_mask_test_t;
static void cache_external_mask(lv_event_t *event) {
    cache_mask_test_t *state=lv_event_get_user_data(event);
    if (lv_event_get_code(event)==LV_EVENT_DRAW_MAIN_BEGIN && state->enabled) {
        lv_area_t area={25,25,386,386};
        lv_draw_mask_radius_init(&state->mask,&area,80,false);
        state->id=lv_draw_mask_add(&state->mask,state);
    } else if (lv_event_get_code(event)==LV_EVENT_DRAW_POST_END && state->id>=0) {
        lv_draw_mask_remove_id(state->id); lv_draw_mask_free_param(&state->mask); state->id=-1;
    }
}

static void test_launcher_background_cache(void) {
    lv_obj_t *previous=lv_scr_act();
    lv_obj_t *screen=lv_obj_create(NULL); lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x050706),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0); lv_scr_load(screen);
    lv_obj_t *underlay=lv_obj_create(screen); lv_obj_remove_style_all(underlay);
    lv_obj_set_size(underlay,412,412);
    lv_obj_set_style_bg_color(underlay,lv_color_hex(0xA0B0C0),0);
    lv_obj_set_style_bg_opa(underlay,LV_OPA_COVER,0);
    lv_obj_add_flag(underlay,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *circle=lv_obj_create(screen); lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle,412,412);
    lv_obj_set_style_bg_color(circle,lv_color_hex(CHRONVS_UI_PANEL),0);
    lv_obj_set_style_bg_opa(circle,LV_OPA_COVER,0);
    lv_obj_set_style_radius(circle,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_clip_corner(circle,true,0);
    bool reference=true;
    cache_mask_test_t mask={.id=-1};
    lv_obj_add_event_cb(circle,cache_external_mask,LV_EVENT_DRAW_MAIN_BEGIN,&mask);
    lv_obj_add_event_cb(circle,cache_external_mask,LV_EVENT_DRAW_POST_END,&mask);
    chronvs_circle_fill_bind(circle,&reference);
    unsigned char *expected=malloc(sizeof(pixels)); assert(expected);
    for (unsigned scenario=0;scenario<12;++scenario) {
        switch (scenario) {
            case 1: lv_obj_set_style_bg_color(circle,lv_color_hex(0x604020),0); break;
            case 2: lv_obj_set_style_bg_color(screen,lv_color_hex(0x104060),0); break;
            case 3: lv_obj_set_pos(circle,0,72); break;
            case 4: lv_obj_set_pos(circle,-63,0); break;
            case 5: lv_obj_set_pos(circle,0,0); lv_obj_set_style_radius(circle,70,0); break;
            case 6: lv_obj_set_style_radius(circle,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_opa(circle,140,0); break;
            case 7: lv_obj_set_style_bg_opa(circle,LV_OPA_COVER,0); lv_obj_clear_flag(underlay,LV_OBJ_FLAG_HIDDEN); break;
            case 8: lv_obj_add_flag(underlay,LV_OBJ_FLAG_HIDDEN); lv_obj_set_size(circle,300,300); break;
            case 9: lv_obj_set_size(circle,412,412); lv_obj_set_style_outline_width(circle,4,0); break;
            case 10: lv_obj_set_style_outline_width(circle,0,0); mask.enabled=true; break;
            case 11:
                mask.enabled=false;
                lv_obj_set_style_bg_color(circle,lv_color_hex(CHRONVS_UI_PANEL),0);
                lv_obj_set_style_bg_color(screen,lv_color_hex(0x050706),0); break;
        }
        reference=true; lv_obj_invalidate(screen); lv_refr_now(NULL);
        memcpy(expected,pixels,sizeof(pixels));
        reference=false;
        for (unsigned render=0;render<2;++render) {
            lv_obj_invalidate(screen); lv_refr_now(NULL);
            assert(!memcmp(expected,pixels,sizeof(pixels)));
        }
        lv_area_t partial={117,43,288,233};
        lv_obj_invalidate_area(circle,&partial); lv_refr_now(NULL);
        assert(!memcmp(expected,pixels,sizeof(pixels)));
    }
    free(expected); lv_scr_load(previous); lv_obj_del(screen); lv_refr_now(NULL);
    puts("Launcher background: reference pixels match after color/backdrop invalidation, motion, partial redraw, preview, size, opacity, outline and external mask; deletion passed.");
}

/* Run the actual pointer pipeline at different sampling intervals, including
 * stalls longer than the launcher's 100 ms filter clamp. No hardware timing
 * claims: the host flush is immediate and has no QSPI transport. */
static void launcher_sample(int y, lv_indev_state_t state, unsigned ms) {
    point=(lv_point_t){210,y}; contact=state;
    lv_tick_inc(ms); lv_timer_handler(); chronvs_panel_autotest_poll();
}

static void check_launcher_rows(lv_obj_t *launcher, lv_obj_t *list, int cycle) {
    lv_obj_update_layout(list);
    const int scroll=lv_obj_get_scroll_y(list);
    /* Input can run after the curve timer in the same handler. Rebasing is
     * deferred to its next 20 ms tick, so inspect native bounds here. */
    assert(scroll>=0 && lv_obj_get_scroll_bottom(list)>=0);
    assert(!strcmp(chronvs_app_active_id(),"apps") && lv_obj_get_y(launcher)==0);
    unsigned visible=0;
    for (unsigned i=0;i<lv_obj_get_child_cnt(list);++i) {
        lv_obj_t *row=lv_obj_get_child(list,i);
        lv_area_t bounds; lv_obj_get_coords(row,&bounds);
        assert((lv_obj_get_y(row)-174-(int)i*82)%cycle==0);
        if (bounds.y1<312 && bounds.y2>100 && lv_obj_get_style_opa_layered(row,0)) ++visible;
    }
    assert(visible>=2);
}

static void test_launcher_stress(lv_obj_t *launcher, lv_obj_t *list, int cycle) {
    const unsigned cadences[]={20,35,70,140};
    lv_indev_t *input=lv_indev_get_next(NULL); assert(input);
    lv_timer_t *reader=input->driver->read_timer;
    const uint32_t original_period=reader->period;
    const unsigned row_count=lv_obj_get_child_cnt(list);
    unsigned char *incremental=malloc(sizeof(pixels)); assert(incremental);
    lv_mem_monitor_t baseline={0}, memory;
    unsigned minimum_free=LV_MEM_SIZE, minimum_biggest=LV_MEM_SIZE;
    uint64_t total_pixels=0;
    unsigned total_refreshes=0;
    for (unsigned round=0;round<5;++round) {
        for (unsigned cadence=0;cadence<sizeof(cadences)/sizeof(cadences[0]);++cadence) {
            const unsigned ms=cadences[cadence];
            lv_timer_set_period(reader,ms);
            for (int direction=-1;direction<=1;direction+=2) {
                lv_obj_scroll_to_y(list,cycle-40,LV_ANIM_OFF); elapse(600);
                const unsigned frames_before=refresh_count;
                const uint64_t pixels_before=flushed_pixels;
                launcher_sample(206,LV_INDEV_STATE_PR,ms);
                /* Fast move, then reverse without lifting, far from the
                 * reserved close strip. */
                for (unsigned leg=0;leg<2;++leg) {
                    const int sign=leg ? -direction : direction;
                    const int start=leg ? 206+direction*120 : 206;
                    const int before=lv_obj_get_scroll_y(list);
                    for (int step=1;step<=4;++step) {
                        launcher_sample(start+sign*step*30,LV_INDEV_STATE_PR,ms);
                        check_launcher_rows(launcher,list,cycle);
                    }
                    /* The input filter may catch up while the finger is held.
                     * Once caught up, holding must cause no scroll/redraw. */
                    for (unsigned hold=0;hold<30;++hold)
                        launcher_sample(start+sign*120,LV_INDEV_STATE_PR,ms);
                    int delta=(lv_obj_get_scroll_y(list)-before+cycle)%cycle;
                    assert(sign<0 ? delta>0 && delta<160 : delta>cycle-160);
                    const int stationary=lv_obj_get_scroll_y(list);
                    const unsigned idle_frames=refresh_count;
                    for (unsigned hold=0;hold<8;++hold)
                        launcher_sample(start+sign*120,LV_INDEV_STATE_PR,ms);
                    assert(lv_obj_get_scroll_y(list)==stationary);
                    assert(refresh_count==idle_frames);
                }
                const int release=lv_obj_get_scroll_y(list);
                launcher_sample(206,LV_INDEV_STATE_REL,ms);
                elapse(600);
                assert(lv_obj_get_scroll_y(list)==release && !lv_obj_is_scrolling(list));
                check_launcher_rows(launcher,list,cycle);
                assert(lv_obj_get_child_cnt(list)==row_count);
                const unsigned frames=refresh_count-frames_before;
                const uint64_t drawn=flushed_pixels-pixels_before;
                total_refreshes+=frames;
                total_pixels+=drawn;
                if (!round)
                    printf("Launcher cadence %u ms, initial direction %+d: %u refreshes, "
                           "%llu pixels/refresh.\n",ms,direction,frames,
                           (unsigned long long)(frames ? drawn/frames : 0));
                memcpy(incremental,pixels,sizeof(pixels));
                lv_obj_invalidate(lv_scr_act()); lv_refr_now(NULL);
                assert(!memcmp(incremental,pixels,sizeof(pixels)));
                lv_mem_monitor(&memory);
                if (memory.free_size<minimum_free) minimum_free=memory.free_size;
                if (memory.free_biggest_size<minimum_biggest) minimum_biggest=memory.free_biggest_size;
            }
        }
        lv_mem_monitor(&memory);
        if (!round) baseline=memory; /* Warm transient drawing/style caches. */
        else {
            assert(memory.free_size==baseline.free_size);
            assert(memory.free_biggest_size==baseline.free_biggest_size);
        }
    }
    lv_timer_set_period(reader,original_period);
    free(incremental);
    elapse(600); refresh_count=0; elapse(2000);
    assert(refresh_count==0);
    printf("Launcher stress: 40 contacts, 20/35/70/140 ms, reversal/hold/release, "
           "incremental pixels equal full redraw; %u refreshes, %llu flushed pixels.\n",
           total_refreshes,(unsigned long long)total_pixels);
    printf("Launcher stress LVGL after gestures: min free %u, min largest block %u; "
           "stable after warmup, idle 0 refreshes/2 s.\n",minimum_free,minimum_biggest);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    lv_init();
    chronvs_Relogio_init();
    set_time(26,9,10,12,0,0); /* Main runtime seeds the shared clock at boot. */
    assert(lvgl_pool_allocations==1);
    assert(chronvs_lvgl_pool_alloc(LV_MEM_SIZE)==chronvs_lvgl_pool_alloc(LV_MEM_SIZE));
    assert(lvgl_pool_allocations==1); /* Reinitialization keeps the same arena. */
    static lv_color_t buffer[412*412/20];static lv_disp_draw_buf_t draw;static lv_disp_drv_t driver;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);lv_disp_drv_init(&driver);
    driver.hor_res=driver.ver_res=412;driver.draw_buf=&draw;driver.flush_cb=flush;
    driver.monitor_cb=count_refresh;
    driver.render_start_cb=count_render_start;
    lv_disp_drv_register(&driver);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER;input.read_cb=read_touch;lv_indev_drv_register(&input);
    chronvs_display_profile_init(); chronvs_display_profile_init();
    chronvs_app_manager_init(lv_scr_act());assert(chronvs_apps_register_all());
    assert(chronvs_app_open("watch"));
#ifndef TEST_CONTINUOUS_LIGHT
    const char *boot_test=getenv("CHRONVS_TEST_LIGHT_BOOT");
    if (boot_test) {
        const int expected=atoi(boot_test);
        const char *mode_env=getenv("CHRONVS_TEST_LIGHT_MODE");
        int mode=mode_env && *mode_env ? atoi(mode_env) :
                 expected==35 ? 0 : expected==50 ? 1 : expected==75 ? 3 : 2;
        const int levels[]={35,50,100,75,35};
        const char *labels[]={"35","50","100","75","35"};
        const int next[]={1,3,4,2,1};
        assert(LCD_Backlight==expected && find_label_text(lv_scr_act(),labels[mode]));
        lv_obj_t *boot_panel=lv_obj_get_child(lv_scr_act(),1);
        lv_obj_clear_flag(boot_panel,LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(boot_panel,0); elapse(100);
        assert((find_label_text(boot_panel,"ECO")!=NULL)==(mode==0));
        for (unsigned i=0;i<4;++i) {
            tap(163,278); mode=next[mode];
            assert(LCD_Backlight==levels[mode] && saved_light_mode==mode);
            assert(find_label_text(boot_panel,labels[mode]));
        }
        printf("Lighting boot and saved cycle passed: %d%%.\n",expected);
        return 0;
    }
#endif
    capture("12-watch");
    unsigned char *frozen_pixels=malloc(sizeof(pixels)); assert(frozen_pixels);
    memcpy(frozen_pixels,pixels,sizeof(pixels));
    refresh_count=0;
    for (unsigned i=0; i<1000; ++i) {
        now_us+=10000;
        lv_tick_inc(10); lv_timer_handler();
    }
    printf("Watch idle: %u refreshes over 10 seconds.\n",refresh_count);
    assert(refresh_count==0);
    assert(frames>=refresh_count && touch_reads>0);
    lv_obj_invalidate(chronvs_app_content_layer()); elapse(100);
    assert(!memcmp(frozen_pixels,pixels,sizeof(pixels))); /* Incidental redraw stays frozen. */
    refresh_count=0;
    touch(206,206,LV_INDEV_STATE_PR); elapse(350);
    assert(refresh_count==0); /* Short contacts do not refresh the snapshot. */
    elapse(350);
    assert(refresh_count==1); /* Hold recognized after 600 ms. */
#ifdef CHRONVS_WATCH_FLAT_BACKGROUND
    assert(!memcmp(frozen_pixels,pixels,sizeof(pixels))); /* Flat diagnostic stays flat. */
#else
    assert(memcmp(frozen_pixels,pixels,sizeof(pixels))); /* Samples the current time. */
#endif
    free(frozen_pixels);
    refresh_count=0; elapse(3000);
    assert(refresh_count==3);
    elapse(16000); assert(LCD_Backlight>12); /* Hold counts as real activity. */
    touch(206,206,LV_INDEV_STATE_REL); elapse(100);
    refresh_count=0; elapse(3000); assert(refresh_count==0);
    touch(206,206,LV_INDEV_STATE_PR); elapse(700);
    touch(230,206,LV_INDEV_STATE_PR); elapse(100);
    refresh_count=0; elapse(2000); assert(refresh_count==0); /* Drift cancels. */
    touch(230,206,LV_INDEV_STATE_REL);
    elapse(46000); assert(LCD_Backlight==0);
    touch(206,206,LV_INDEV_STATE_PR); elapse(100);
    refresh_count=0; elapse(2500); assert(refresh_count==0); /* Wake-only hold. */
    touch(206,206,LV_INDEV_STATE_REL);
    chronvs_system_ui_notify_activity();
    lv_obj_t *panel=lv_obj_get_child(lv_scr_act(),1);assert(panel);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(panel,0);
    chronvs_system_ui_set_battery(72,3.9f);capture("13-quick-settings");
    assert(lv_obj_get_style_radius(panel,0)==LV_RADIUS_CIRCLE);
#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
    assert(!lv_obj_get_style_clip_corner(panel,0));
#else
    assert(lv_obj_get_style_clip_corner(panel,0));
#endif
#ifdef TEST_CONTINUOUS_LIGHT
    /* Compare every brightness angle, with the circle clipped by screen edges.
     * Setters exercise paint only; the saved preference stays unchanged. */
    lv_obj_t *paint_arc=lv_obj_get_child(panel,0);
    const int paint_value=paint_brightness_get(paint_arc);
    const int paint_positions[]={-210,-100,0};
    for (int value=10;value<=100;value+=5) {
        paint_brightness_set(paint_arc,value);
        for (unsigned i=0;i<3;++i) {
            lv_obj_set_y(panel,paint_positions[i]);
            char name[72]; snprintf(name,sizeof(name),"arc-brightness-%03d-y%03d",value,-paint_positions[i]);
            capture(name);
        }
    }
    paint_brightness_set(paint_arc,paint_value);
#else
    lv_obj_t *lighting=lv_obj_get_child(panel,0);
    assert(lv_obj_check_type(lighting,&lv_btn_class));
    assert(lv_obj_get_width(lighting)==70 && lv_obj_get_height(lighting)==70);
    assert(lv_obj_get_x(lighting)==128 && lv_obj_get_y(lighting)==243);
    assert(find_label_text(lighting,"50"));
    tap(163,267); assert(LCD_Backlight==75 && saved_light_mode==3); /* Sun tap. */
    assert(find_label_text(lighting,"75")); capture("lighting-75");
    tap(163,278); assert(LCD_Backlight==100 && saved_light_mode==2);
    assert(find_label_text(lighting,"100")); capture("lighting-100");
    tap(163,278); assert(LCD_Backlight==35 && saved_light_mode==4);
    assert(find_label_text(lighting,"35") && !find_label_text(panel,"ECO"));
    elapse(6000); assert(LCD_Backlight==35); /* Selecting 35% does not enable ECO. */
    tap(249,123); assert(LCD_Backlight==35 && saved_light_mode==0); /* Battery symbol tap. */
    assert(find_label_text(lighting,"35") && find_label_text(panel,"ECO")); capture("lighting-eco");
    tap(249,134); assert(LCD_Backlight==35 && saved_light_mode==4);
    assert(!find_label_text(panel,"ECO")); capture("lighting-35-normal");
    elapse(6000); assert(LCD_Backlight==35); /* ECO timing no longer applies. */
    tap(249,134); assert(LCD_Backlight==35 && saved_light_mode==0);
    tap(163,278); assert(LCD_Backlight==50 && saved_light_mode==1);
    assert(!find_label_text(panel,"ECO"));
    capture("lighting-50");
#endif
    lv_obj_set_y(panel,-160); capture("quick-settings-circle-transition");
    rect_draws=0;
    for (int y=-400;y<=0;y+=40) {
        char name[64]; snprintf(name,sizeof(name),"circle-panel-%d",-y);
        lv_obj_set_y(panel,y); lv_obj_invalidate(lv_scr_act()); capture(name);
    }
    printf("Circular panel rectangle draws: %u\n",rect_draws);
    lv_obj_set_y(panel,0);
    unsigned circles=0;
    for(unsigned i=0;i<lv_obj_get_child_cnt(panel);++i) {
        lv_obj_t *child=lv_obj_get_child(panel,i);
        if(lv_obj_check_type(child,&lv_btn_class)) {
            assert(lv_obj_get_width(child)==CHRONVS_UI_CIRCLE_SIZE);
            assert(lv_obj_get_height(child)==CHRONVS_UI_CIRCLE_SIZE);++circles;
        }
    }
    assert(circles==7);
    /* The quick indicator reads persisted data before the weather app is opened. */
    lv_obj_t *weather_caption=find_label_text(panel,"CLIMA"); assert(weather_caption);
    lv_obj_t *weather_slot=lv_obj_get_parent(weather_caption);
    lv_obj_t *weather_icon=lv_obj_get_child(weather_slot,0);
    assert(lv_obj_has_flag(weather_slot,LV_OBJ_FLAG_CLICKABLE));
    assert(lv_obj_has_flag(weather_icon,LV_OBJ_FLAG_HIDDEN));
    assert(weather_queries==0 && weather_result_reads==0);
    saved_weather=(chronvs_weather_snapshot_t){.valid=true,.temperature_c=23,.weather_code=2};
    elapse(600);
    assert(find_label_text(weather_slot,"23°") && !lv_obj_has_flag(weather_icon,LV_OBJ_FLAG_HIDDEN));
    capture("24-quick-weather-cache");
    /* All symbols and extreme temperatures stay within the 70 px circle. */
    const int weather_codes[]={0,2,3,45,61,71,95};
    for (unsigned i=0;i<sizeof(weather_codes)/sizeof(weather_codes[0]);++i) {
        saved_weather.weather_code=weather_codes[i];
        saved_weather.temperature_c=i==0 ? -90 : 70;
        elapse(600);
        char name[64]; snprintf(name,sizeof(name),"quick-weather-icon-%d",weather_codes[i]);
        capture(name);
        lv_area_t slot_area, caption_area, icon_area;
        lv_obj_get_coords(weather_slot,&slot_area);
        lv_obj_get_coords(weather_caption,&caption_area);
        lv_obj_get_coords(weather_icon,&icon_area);
        assert(icon_area.y2<caption_area.y1);
        const lv_area_t areas[]={caption_area,icon_area};
        for (unsigned j=0;j<2;++j) {
            int dx=LV_MAX(abs(areas[j].x1-(slot_area.x1+35)),abs(areas[j].x2-(slot_area.x1+35)));
            int dy=LV_MAX(abs(areas[j].y1-(slot_area.y1+35)),abs(areas[j].y2-(slot_area.y1+35)));
            assert(dx*dx+dy*dy<=35*35);
        }
    }
    weather_state=CHRONVS_WEATHER_ERROR; elapse(600);
    assert(find_label_text(weather_slot,"70°")); /* Failed refresh keeps saved data. */
    assert(weather_queries==0 && weather_result_reads==0 && weather_rtc_reads==0);
    assert(!strcmp(chronvs_app_active_id(),"watch"));
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    unsigned cache_reads_before=weather_cache_reads;
    elapse(600); assert(weather_cache_reads==cache_reads_before);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);
    saved_weather=(chronvs_weather_snapshot_t){0}; weather_state=CHRONVS_WEATHER_IDLE;
    elapse(600);
    assert(find_label_text(weather_slot,"CLIMA") && lv_obj_has_flag(weather_icon,LV_OBJ_FLAG_HIDDEN));
    assert(sound_volume == 4); /* Restore the saved preference at boot. */
    assert(previews == 0);
    const uint8_t levels[] = {5, 0, 1, 2, 3, 4};
    for (unsigned i = 0; i < sizeof(levels); ++i) {
        tap(120,206);
        assert(sound_volume == levels[i] && saved_volume == levels[i]);
        assert(previews == i + 1 && preview_volume == levels[i]);
        if (levels[i] == 0) {
            assert(find_label_text(panel, LV_SYMBOL_MUTE));
            capture("21-volume-muted");
        }
    }
    capture("22-volume-control");
    elapse(46000); assert(LCD_Backlight == 0);
    cache_reads_before=weather_cache_reads;
    elapse(1000); assert(weather_cache_reads==cache_reads_before);
    tap(292,206); assert(weather_queries==0 && !strcmp(chronvs_app_active_id(),"watch"));
    assert(sound_volume == 4 && LCD_Backlight == TEST_NORMAL_BRIGHTNESS); /* Wake without opening Clima. */
    assert(previews == 6);
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    assert(chronvs_app_open("apps"));capture("14-app-list");
    lv_obj_t *launcher = lv_obj_get_child(chronvs_app_content_layer(), -1);
    assert(lv_obj_get_style_radius(launcher,0)==LV_RADIUS_CIRCLE);
#ifdef CHRONVS_PANEL_SELECTIVE_CLIP
    assert(!lv_obj_get_style_clip_corner(launcher,0));
#else
    assert(lv_obj_get_style_clip_corner(launcher,0));
#endif
    assert(chronvs_app_open("watch"));
    assert(chronvs_app_preview_y("apps",160));
    capture("launcher-circle-transition");
    rect_draws=0;
    for (int y=400;y>=0;y-=40) {
        char name[64]; snprintf(name,sizeof(name),"circle-launcher-%d",y);
        chronvs_app_preview_y("apps",y); lv_obj_invalidate(lv_scr_act()); capture(name);
    }
    printf("Circular launcher rectangle draws: %u\n",rect_draws);
    chronvs_app_cancel_preview();
    assert(chronvs_app_open("apps"));
    lv_obj_t *app_list = lv_obj_get_child(launcher, 0);
    assert(lv_obj_get_child_cnt(app_list) == 7);
    const int launcher_cycle = 7 * 82;
    lv_obj_t *first_row = lv_obj_get_child(app_list, 0);
    lv_obj_t *middle_row = lv_obj_get_child(app_list, 1);
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
    if (getenv("CHRONVS_TEST_ICON_CACHE_FAIL") == NULL) {
        lv_obj_scroll_to_y(app_list, 0, LV_ANIM_OFF); elapse(70);
        lv_obj_t *cached_badge = lv_obj_get_child(first_row, 0);
        assert(lv_obj_get_child_cnt(cached_badge) == 2);
        lv_obj_t *vector_icon = lv_obj_get_child(cached_badge, 0);
        lv_obj_t *cached_image = lv_obj_get_child(cached_badge, 1);
        assert(lv_obj_has_flag(vector_icon, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(cached_image, LV_OBJ_FLAG_HIDDEN));
        lv_obj_scroll_to_y(app_list, 164, LV_ANIM_OFF); elapse(70);
        assert(!lv_obj_has_flag(vector_icon, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(cached_image, LV_OBJ_FLAG_HIDDEN));
#ifdef CHRONVS_LAUNCHER_ICON_OPAQUE
        unsigned opaque_icons=0;
        for (unsigned i=0;i<lv_obj_get_child_cnt(app_list);++i) {
            lv_obj_t *badge=lv_obj_get_child(lv_obj_get_child(app_list,i),0);
            const lv_img_dsc_t *image=lv_img_get_src(lv_obj_get_child(badge,1));
            if (image->header.cf==LV_IMG_CF_TRUE_COLOR) {
                assert(image->header.w==40 && image->header.h==40);
                ++opaque_icons;
            }
        }
        assert(opaque_icons>0);
        printf("Opaque icon fast path: %u of %u icons.\n",opaque_icons,(unsigned)lv_obj_get_child_cnt(app_list));
#endif
    } else {
        assert(lv_obj_get_child_cnt(lv_obj_get_child(first_row, 0)) == 1);
    }
#endif
#if defined(CHRONVS_PANEL_NO_CONTENT) || defined(CHRONVS_PANEL_NO_ICONS)
    /* A centered row keeps its hit target; verify icon and text separately. */
    lv_obj_scroll_to_y(app_list, 0, LV_ANIM_OFF); elapse(70);
    lv_obj_t *omitted_icon = lv_obj_get_child(lv_obj_get_child(first_row, 0), 0);
    lv_obj_t *omitted_name = lv_obj_get_child(first_row, 1);
    assert(lv_obj_get_style_opa_layered(first_row, 0) == LV_OPA_COVER);
    lv_obj_add_event_cb(omitted_icon, count_transparent_label_draw, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    transparent_label_draws = 0;
    lv_obj_invalidate(app_list); lv_refr_now(NULL);
    assert(transparent_label_draws == 0);
    lv_obj_remove_event_cb(omitted_icon, count_transparent_label_draw);
    lv_obj_add_event_cb(omitted_name, count_transparent_label_draw, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    transparent_label_draws = 0;
    lv_obj_invalidate(app_list); lv_refr_now(NULL);
#ifdef CHRONVS_PANEL_NO_ICONS
    assert(transparent_label_draws > 0);
#else
    assert(transparent_label_draws == 0);
#endif
    lv_obj_remove_event_cb(omitted_name, count_transparent_label_draw);
    lv_obj_scroll_to_y(app_list, 164, LV_ANIM_OFF); elapse(70);
#endif
    assert(lv_obj_get_style_translate_x(first_row, 0) >
           lv_obj_get_style_translate_x(middle_row, 0));
    lv_obj_scroll_to_y(app_list, 0, LV_ANIM_OFF); elapse(70);
    int centered_x = lv_obj_get_style_translate_x(first_row, 0);
    lv_obj_scroll_to_y(app_list, 82, LV_ANIM_OFF); elapse(70);
    assert(lv_obj_get_style_translate_x(first_row, 0) > centered_x);
    elapse(100); refresh_count=0;
    for (unsigned i=0;i<10;++i) { lv_event_send(app_list,LV_EVENT_SCROLL,NULL); elapse(35); }
    assert(refresh_count==0); /* Unchanged curve must not invalidate row styles. */
    /* Include the fade endpoints and rows still on-screen with zero opacity. */
    const int opacity_offsets[] = {0,8,33,34,40,48,81,82,100,130,131,164,177,178,179,180,181,200,246,328,410};
    for (unsigned i=0;i<sizeof(opacity_offsets)/sizeof(opacity_offsets[0]);++i) {
        lv_obj_scroll_to_y(app_list,opacity_offsets[i],LV_ANIM_OFF); elapse(70);
        char name[64]; snprintf(name,sizeof(name),"launcher-opacity-%03d",opacity_offsets[i]);
        capture(name);
        /* A row's pressed surface must also respect the moving circle,
         * including at the fading edges. These states do not send clicks. */
        lv_obj_add_state(middle_row, LV_STATE_PRESSED);
        snprintf(name,sizeof(name),"launcher-pressed-%03d",opacity_offsets[i]);
        capture(name);
        lv_obj_clear_state(middle_row, LV_STATE_PRESSED);
    }
#if !defined(CHRONVS_PANEL_NO_CONTENT) && !defined(CHRONVS_PANEL_NO_ICONS)
    /* Check rendered vector pixels, rather than just style properties: raw
     * custom drawing must lose contrast at both ends of the circular list. */
    for (unsigned i=0; i<lv_obj_get_child_cnt(app_list); ++i) {
        lv_obj_t *badge=lv_obj_get_child(lv_obj_get_child(app_list,i),0);
        lv_obj_scroll_to_y(app_list,launcher_cycle+(int)i*82,LV_ANIM_OFF); elapse(70);
        lv_refr_now(NULL);
        const int full=icon_peak_contrast(badge);
        for (int side=-1; side<=1; side+=2) {
            lv_obj_scroll_to_y(app_list,launcher_cycle+(int)i*82+side*154,LV_ANIM_OFF); elapse(70);
            lv_refr_now(NULL);
            const int faded=icon_peak_contrast(badge);
            printf("Launcher icon %u side %d: contrast %d -> %d.\n",i,side,full,faded);
            assert(faded>full/4 && faded<full*3/4);
        }
    }
#endif
    lv_obj_scroll_to_y(app_list,180,LV_ANIM_OFF); elapse(70);
    assert(lv_obj_get_style_opa_layered(first_row,0)==LV_OPA_TRANSP);
    assert(first_row->coords.y2 >= app_list->coords.y1);
    lv_obj_t *transparent_name = lv_obj_get_child(first_row,1);
    assert(lv_obj_check_type(transparent_name,&lv_label_class));
    lv_obj_add_event_cb(transparent_name,count_transparent_label_draw,LV_EVENT_DRAW_MAIN_BEGIN,NULL);
    transparent_label_draws=0;
    lv_obj_invalidate(app_list); lv_refr_now(NULL);
    printf("Transparent launcher label: %u draw callbacks.\n",transparent_label_draws);
    assert(transparent_label_draws==0);
    lv_obj_remove_event_cb(transparent_name,count_transparent_label_draw);
    lv_obj_scroll_to_y(app_list,82,LV_ANIM_OFF); elapse(70);
    capture("19-launcher-arc");
    /* A vertical drag on an app scrolls the arc without launching on release. */
    touch(150,206,LV_INDEV_STATE_PR);
    touch(150,170,LV_INDEV_STATE_PR);
    touch(150,120,LV_INDEV_STATE_PR);
    touch(150,120,LV_INDEV_STATE_REL); elapse(500);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    assert(lv_obj_get_scroll_y(app_list) > 82);
    capture("20-launcher-scrolled");
    /* Filter both signs of input, then inspect every frame after release:
     * native rows must remain visible and attached to the list's scroll. */
    for (unsigned direction_index=0; direction_index<2; ++direction_index) {
        const int direction=direction_index ? 1 : -1;
        lv_obj_scroll_to_y(app_list,246,LV_ANIM_OFF); elapse(600);
        touch(210,206,LV_INDEV_STATE_PR);
        touch(210,206+direction*40,LV_INDEV_STATE_PR);
        int displacement=lv_obj_get_scroll_y(app_list)-launcher_cycle-246;
        assert(direction<0 ? displacement>0 && displacement<40 : displacement<0 && displacement > -40);
        touch(210,206+direction*80,LV_INDEV_STATE_PR);
        touch(210,206+direction*80,LV_INDEV_STATE_REL);
        for (unsigned frame=0; frame<30; ++frame) {
            elapse(35); lv_obj_update_layout(app_list);
            const int scroll=lv_obj_get_scroll_y(app_list);
            assert(scroll>=0 && lv_obj_get_scroll_bottom(app_list)>=0);
            assert(lv_obj_get_y(launcher)==0 && !strcmp(chronvs_app_active_id(),"apps"));
            lv_area_t list_bounds; lv_obj_get_coords(app_list,&list_bounds);
            unsigned visible=0;
            for (unsigned row_index=0; row_index<lv_obj_get_child_cnt(app_list); ++row_index) {
                lv_obj_t *row=lv_obj_get_child(app_list,row_index);
                lv_area_t bounds; lv_obj_get_coords(row,&bounds);
                assert(!lv_obj_has_flag(row,LV_OBJ_FLAG_FLOATING));
                assert((bounds.y1-list_bounds.y1+scroll-174-(int)row_index*82)%launcher_cycle==0);
                if (bounds.y2>100 && bounds.y1<312 && lv_obj_get_style_opa_layered(row,0)>0) ++visible;
            }
            assert(visible>0);
        }
        char sample_name[48]; snprintf(sample_name,sizeof(sample_name),"launcher-input-release-%u",direction_index);
        capture(sample_name);
        unsigned char *settled_pixels=malloc(sizeof(pixels)); assert(settled_pixels);
        memcpy(settled_pixels,pixels,sizeof(pixels));
        lv_obj_invalidate(lv_scr_act()); lv_refr_now(NULL);
        assert(!memcmp(settled_pixels,pixels,sizeof(pixels)));
        free(settled_pixels);
    }
    /* A fast finish near an edge must not fling through the remaining list. */
    const int fling_paths[2][5]={{340,280,200,100,65},{80,140,220,320,350}};
    for (unsigned direction_index=0; direction_index<2; ++direction_index) {
        int maximum=lv_obj_get_scroll_top(app_list)+lv_obj_get_scroll_bottom(app_list);
        lv_obj_scroll_to_y(app_list,direction_index ? maximum : 0,LV_ANIM_OFF); elapse(600);
        for (unsigned sample=0; sample<5; ++sample)
            touch(210,fling_paths[direction_index][sample],LV_INDEV_STATE_PR);
        const int release_scroll=lv_obj_get_scroll_y(app_list);
        touch(210,fling_paths[direction_index][4],LV_INDEV_STATE_REL);
        elapse(1200);
        int coast=lv_obj_get_scroll_y(app_list)-release_scroll;
        assert(coast==0);
        assert(!strcmp(chronvs_app_active_id(),"apps") && lv_obj_get_y(launcher)==0);
        printf("Launcher fast release %s: %d px coast.\n",direction_index ? "down" : "up",coast);
    }
    /* Traverse several complete cycles in both directions. Every row keeps
     * its app identity and modulo spacing, without hitting an endpoint. */
    for (unsigned boundary=0; boundary<2; ++boundary) {
        const int direction=boundary ? -1 : 1;
        lv_obj_scroll_to_y(app_list,0,LV_ANIM_OFF); elapse(600);
        int travel=0, wraps=0;
        for (unsigned pass=0; pass<24; ++pass) {
            const int before=lv_obj_get_scroll_y(app_list);
            const int start=direction>0 ? 100 : 310;
            touch(210,start,LV_INDEV_STATE_PR);
            for (unsigned sample=1; sample<=4; ++sample)
                touch(210,start+direction*(int)sample*40,LV_INDEV_STATE_PR);
            touch(210,start+direction*160,LV_INDEV_STATE_REL); elapse(100);
            const int after=lv_obj_get_scroll_y(app_list);
            int delta=(before-after)*direction;
            if (delta<0) { delta+=launcher_cycle; ++wraps; }
            assert(delta>0 && delta<200);
            travel+=delta;
            assert(after>=launcher_cycle && after<2*launcher_cycle);
            assert(!strcmp(chronvs_app_active_id(),"apps") && lv_obj_get_y(launcher)==0);
            assert(lv_obj_get_child_cnt(app_list)==7);
        }
        assert(wraps>=3 && travel>3*launcher_cycle);
    }
    /* The last app remains selectable after wrapping; reopening preserves
     * the circular position rather than jumping to the first app. */
    const char *last_id = NULL;
    for (size_t i=0; i<chronvs_app_count(); ++i)
        if (chronvs_app_at(i)->launcher_visible) last_id=chronvs_app_at(i)->id;
    assert(last_id);
    lv_obj_scroll_to_y(app_list,launcher_cycle-82,LV_ANIM_OFF); elapse(600);
    const int last_position=lv_obj_get_scroll_y(app_list);
    tap(150,206); assert(!strcmp(chronvs_app_active_id(),last_id));
    assert(chronvs_app_open("apps")); elapse(100);
    assert(lv_obj_get_scroll_y(app_list)==last_position);
    /* A slide beginning in a gap must not become a new tap on a row. */
    lv_obj_scroll_to_y(app_list,0,LV_ANIM_OFF); elapse(600);
    touch(210,170,LV_INDEV_STATE_PR);
    touch(210,190,LV_INDEV_STATE_PR);
    touch(210,190,LV_INDEV_STATE_REL); elapse(150);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    /* Horizontal movement is still a slide, although the list is vertical. */
    touch(150,206,LV_INDEV_STATE_PR);
    touch(190,206,LV_INDEV_STATE_PR);
    touch(190,206,LV_INDEV_STATE_REL); elapse(150);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    /* Returning to the starting point must not turn a slide back into a tap. */
    touch(150,206,LV_INDEV_STATE_PR);
    touch(159,206,LV_INDEV_STATE_PR);
    touch(150,206,LV_INDEV_STATE_PR);
    touch(150,206,LV_INDEV_STATE_REL); elapse(150);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    /* Retouching immediately after a slide must not select the row. */
    lv_obj_scroll_to_y(app_list,82,LV_ANIM_OFF); elapse(600);
    touch(210,206,LV_INDEV_STATE_PR);
    touch(210,160,LV_INDEV_STATE_PR);
    touch(210,110,LV_INDEV_STATE_PR);
    touch(210,110,LV_INDEV_STATE_REL);
    assert(!lv_obj_is_scrolling(app_list));
    lv_obj_update_layout(app_list);
    int stop_y=206;
    for (unsigned row_index=0; row_index<lv_obj_get_child_cnt(app_list); ++row_index) {
        lv_area_t row_bounds;
        lv_obj_get_coords(lv_obj_get_child(app_list,row_index),&row_bounds);
        int center=(row_bounds.y1+row_bounds.y2)/2;
        if (center>=150 && center<=260) { stop_y=center; break; }
    }
    tap(210,stop_y);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    elapse(600);
    /* Retouch a partially clipped row: focusing it must not animate the list
     * underneath a stationary finger before the next scroll gesture. */
    lv_obj_scroll_to_y(app_list,60,LV_ANIM_OFF); elapse(600);
    const int retouch_scroll=lv_obj_get_scroll_y(app_list);
    touch(210,390,LV_INDEV_STATE_PR);
    for (unsigned sample=0; sample<12; ++sample) {
        elapse(35);
        assert(lv_obj_get_y(launcher)==0);
        assert(lv_obj_get_scroll_y(app_list)==retouch_scroll);
    }
    touch(210,350,LV_INDEV_STATE_PR);
    touch(210,310,LV_INDEV_STATE_PR);
    touch(210,310,LV_INDEV_STATE_REL); elapse(600);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    /* Repeated flings and retouches must stop momentum while held, then
     * follow each new direction without moving the launcher itself. */
    for (unsigned pass=0; pass<12; ++pass) {
        const int direction=pass%2 ? 1 : -1;
        touch(210,206,LV_INDEV_STATE_PR);
        int previous=lv_obj_get_scroll_y(app_list);
        for (unsigned sample=0; sample<4; ++sample) {
            elapse(35);
            assert(lv_obj_get_scroll_y(app_list)==previous);
        }
        for (unsigned sample=1; sample<=3; ++sample) {
            touch(210,206+direction*(int)sample*30,LV_INDEV_STATE_PR);
            int current=lv_obj_get_scroll_y(app_list);
            int delta=(current-previous+launcher_cycle)%launcher_cycle;
            assert(direction<0 ? delta<100 : delta==0 || delta>launcher_cycle-100);
            assert(lv_obj_get_y(launcher)==0);
            previous=current;
        }
        touch(210,206+direction*90,LV_INDEV_STATE_REL); elapse(70);
        assert(!strcmp(chronvs_app_active_id(),"apps"));
    }
    test_launcher_stress(launcher,app_list,launcher_cycle);
    elapse(600);
    lv_obj_scroll_to_y(app_list, 0, LV_ANIM_OFF); elapse(100);
    const char *first_id = NULL;
    for (size_t i = 0; i < chronvs_app_count(); ++i) {
        if (chronvs_app_at(i)->launcher_visible) { first_id = chronvs_app_at(i)->id; break; }
    }
    assert(first_id);
    tap(40,206); assert(!strcmp(chronvs_app_active_id(), first_id)); /* Icon. */
    assert(chronvs_app_open("apps")); elapse(70);
    tap(150,206); assert(!strcmp(chronvs_app_active_id(), first_id)); /* Name. */
    assert(chronvs_app_open("apps")); elapse(70);
    touch(206,30,LV_INDEV_STATE_PR); touch(206,80,LV_INDEV_STATE_PR);
    touch(206,80,LV_INDEV_STATE_REL); elapse(70);
    assert(chronvs_app_is_settling() && lv_obj_get_y(launcher)>0 && lv_obj_get_y(launcher)<50);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    tap(40,206); /* A new contact during settling cannot launch an app. */
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    elapse(250);
    assert(!strcmp(chronvs_app_active_id(), "apps") && lv_obj_get_y(launcher) == 0);
    touch(206,30,LV_INDEV_STATE_PR); touch(206,90,LV_INDEV_STATE_PR);
    touch(206,170,LV_INDEV_STATE_PR); touch(206,170,LV_INDEV_STATE_REL); elapse(70);
    assert(chronvs_app_is_settling() && lv_obj_get_y(launcher)>140 && lv_obj_get_y(launcher)<412);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    elapse(250);
    assert(!strcmp(chronvs_app_active_id(), "watch"));
    assert(chronvs_app_preview_y("apps",200));
    assert(chronvs_app_settle_y("apps",0,"apps",220,lv_anim_path_ease_out));
    elapse(70);
    assert(chronvs_app_is_settling() && !strcmp(chronvs_app_active_id(),"watch"));
    assert(lv_obj_get_y(launcher)>0 && lv_obj_get_y(launcher)<200);
    assert(chronvs_app_open(first_id)); /* Other navigation cancels a pending commit. */
    elapse(300);
    assert(!chronvs_app_is_settling() && !strcmp(chronvs_app_active_id(),first_id));
    assert(lv_obj_has_flag(launcher,LV_OBJ_FLAG_HIDDEN));
    assert(chronvs_app_open("Relogio"));
    assert(chronvs_app_open("Notas"));capture("15-Notas-integrated");
    tap(206,341); /* Nova nota. */
    lv_obj_t *text=find_textarea(chronvs_app_content_layer()); assert(text);
    /* Real power timer: continuous typing exceeds both AUTO deadlines. */
    for(unsigned i=0;i<50;++i) {
        tap(76,226); elapse(1000); assert(LCD_Backlight==TEST_NORMAL_BRIGHTNESS);
    }
    assert(Notas_text_length(lv_textarea_get_text(text))==50);
    elapse(16000); assert(LCD_Backlight==12);
    tap(150,178); assert(LCD_Backlight==TEST_NORMAL_BRIGHTNESS);
    elapse(46000); assert(chronvs_system_ui_display_is_off() && LCD_Backlight==0);
    unsigned length=Notas_text_length(lv_textarea_get_text(text));
    tap(150,178); assert(LCD_Backlight==TEST_NORMAL_BRIGHTNESS);
    assert(Notas_text_length(lv_textarea_get_text(text))==length); /* Wake only. */
    tap(76,226); assert(Notas_text_length(lv_textarea_get_text(text))==length+1);
    /* Toggle the actual ECO control, then hold a key past its 15 s deadline. */
    lv_obj_t *eco=NULL;
    for(unsigned i=0;i<lv_obj_get_child_cnt(panel);++i) {
        lv_obj_t *obj=lv_obj_get_child(panel,i);
        if(lv_obj_check_type(obj,&lv_btn_class) && lv_obj_get_x(obj)==214 && lv_obj_get_y(obj)==99) eco=obj;
    }
    assert(eco);
#ifdef TEST_CONTINUOUS_LIGHT
    lv_event_send(eco,LV_EVENT_CLICKED,NULL);
#else
    lv_event_send(eco,LV_EVENT_CLICKED,NULL);
#endif
    assert(LCD_Backlight==35);
    touch(76,226,LV_INDEV_STATE_PR);
    for(unsigned i=0;i<20;++i) { elapse(1000); assert(LCD_Backlight==35); }
    touch(76,226,LV_INDEV_STATE_REL);
    elapse(6000); assert(LCD_Backlight==5);
    elapse(10000); assert(LCD_Backlight==0);
    length=Notas_text_length(lv_textarea_get_text(text));
    tap(150,178); assert(LCD_Backlight==35);
    assert(Notas_text_length(lv_textarea_get_text(text))==length);
    capture("16-Notas-keyboard-integrated");
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    assert(memory.free_biggest_size > 16384);
    printf("All apps retained: %u bytes of LVGL heap free.\n", (unsigned)memory.free_size);
    assert(chronvs_app_open("apps"));
    assert(chronvs_app_open("calculator"));
    lv_obj_t *calculator = lv_obj_get_child(chronvs_app_content_layer(), -1);
    lv_obj_t *calculator_display = lv_obj_get_child(calculator, 0);
    assert(find_label_text(calculator, "DEL") &&
           lv_obj_check_type(calculator_display, &lv_label_class) &&
           !strcmp(lv_label_get_text(calculator_display), "0"));
    capture("17-calculator-integrated");
    /* ECO applies to the calculator too; waking consumes the first key. */
    elapse(16000); assert(LCD_Backlight == 0);
    tap(113,175); assert(LCD_Backlight == 35);
    assert(!strcmp(lv_label_get_text(calculator_display), "0"));
    capture("18-calculator-awake");
    tap(113,175); assert(!strcmp(lv_label_get_text(calculator_display), "7"));
    tap(51,175); assert(!strcmp(lv_label_get_text(calculator_display), "0"));
    tap(51,233); tap(113,291); tap(299,349); tap(175,291); tap(361,233);
    assert(!strcmp(lv_label_get_text(calculator_display), "(1+2)"));
    tap(237,349); assert(!strcmp(lv_label_get_text(calculator_display), "3"));
    tap(361,175); assert(!strcmp(lv_label_get_text(calculator_display), "0"));
    touch(113,175,LV_INDEV_STATE_PR);
    for (unsigned i = 0; i < 20; ++i) { elapse(1000); assert(LCD_Backlight == 35); }
    touch(113,175,LV_INDEV_STATE_REL);
    assert(chronvs_app_open("weather")); capture("23-weather-integrated");
    assert(weather_queries==1 && find_label_text(chronvs_app_content_layer(),"Atualizando..."));
    elapse(16000); assert(LCD_Backlight==0);
    unsigned rtc_before=weather_rtc_reads;
    /* The global wake guard consumes the entire first swipe, including release. */
    touch(120,180,LV_INDEV_STATE_PR); touch(170,180,LV_INDEV_STATE_PR);
    touch(220,180,LV_INDEV_STATE_PR); touch(220,180,LV_INDEV_STATE_REL);
    assert(!strcmp(chronvs_app_active_id(),"weather") && weather_queries==1);
    assert(weather_rtc_reads==rtc_before);
    touch(120,180,LV_INDEV_STATE_PR); touch(170,180,LV_INDEV_STATE_PR);
    touch(220,180,LV_INDEV_STATE_PR); touch(220,180,LV_INDEV_STATE_REL);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    assert(chronvs_app_open("apps"));
    assert(chronvs_app_open("Relogio"));
    assert(chronvs_app_open("Notas"));
    lv_mem_monitor(&memory); assert(memory.free_biggest_size > 16384);
    /* Brightness owns its contact even after moving upward and leaving it. */
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(panel, 0);
    lv_obj_t *brightness = lv_obj_get_child(panel, 0);
#ifdef CHRONVS_BRIGHTNESS_ARC_REFERENCE
    assert(lv_obj_check_type(brightness, &lv_arc_class));
    const int brightness_before = lv_arc_get_value(brightness);
    touch(20,206,LV_INDEV_STATE_PR);
    touch(40,130,LV_INDEV_STATE_PR);
    assert(lv_arc_get_value(brightness) != brightness_before);
    assert(lv_obj_get_y(panel)==0);
    touch(80,75,LV_INDEV_STATE_PR);
    touch(120,70,LV_INDEV_STATE_PR); /* Move into the panel interior before release. */
    touch(120,70,LV_INDEV_STATE_REL); elapse(300);
#elif defined(CHRONVS_BRIGHTNESS_SLIDER_REFERENCE)
    assert(lv_obj_check_type(brightness, &lv_slider_class));
    assert(lv_obj_get_width(brightness)==170 && lv_obj_get_height(brightness)==6);
    assert(lv_obj_get_y(brightness)==70);
    const int wake_brightness=lv_slider_get_value(brightness);
    elapse(16000); assert(chronvs_system_ui_display_is_off());
    tap(120,73);
    assert(!chronvs_system_ui_display_is_off() && lv_slider_get_value(brightness)==wake_brightness);
    touch(120,73,LV_INDEV_STATE_PR);
    touch(80,73,LV_INDEV_STATE_PR);
    assert(lv_slider_get_value(brightness)==10 && LCD_Backlight==10);
    assert(lv_obj_get_y(panel)==0);
    touch(300,73,LV_INDEV_STATE_PR);
    assert(lv_slider_get_value(brightness)==35 && LCD_Backlight==35); /* ECO cap. */
    touch(300,30,LV_INDEV_STATE_PR);
    touch(310,180,LV_INDEV_STATE_PR); /* The contact remains locked to brightness. */
    touch(310,180,LV_INDEV_STATE_REL); elapse(300);
    capture("quick-settings-brightness-slider");
#else
    assert(lv_obj_check_type(brightness,&lv_btn_class));
    assert(find_label_text(brightness,"35") && find_label_text(panel,"ECO"));
    elapse(16000); assert(chronvs_system_ui_display_is_off());
    tap(249,134); assert(LCD_Backlight==35 && saved_light_mode==0); /* Battery wakes only. */
    touch(249,134,LV_INDEV_STATE_PR);
    elapse(16000); assert(LCD_Backlight==35 && saved_light_mode==0); /* No repeat during hold. */
    touch(249,134,LV_INDEV_STATE_REL);
    assert(LCD_Backlight==35 && saved_light_mode==4 && !find_label_text(panel,"ECO"));
    tap(249,134); assert(LCD_Backlight==35 && saved_light_mode==0);
    elapse(16000); assert(chronvs_system_ui_display_is_off());
    tap(163,278); assert(LCD_Backlight==35 && saved_light_mode==0); /* Wake only. */
    touch(163,278,LV_INDEV_STATE_PR);
    elapse(16000); assert(LCD_Backlight==35 && saved_light_mode==0); /* No repeat; real hold is activity. */
    touch(163,278,LV_INDEV_STATE_REL);
    assert(LCD_Backlight==50 && saved_light_mode==1);
    tap(163,278); assert(LCD_Backlight==75 && saved_light_mode==3);
    tap(163,278); assert(LCD_Backlight==100 && saved_light_mode==2);
    tap(249,134); assert(LCD_Backlight==35 && saved_light_mode==0);
    touch(163,278,LV_INDEV_STATE_PR);
    touch(163,243,LV_INDEV_STATE_PR);
    touch(163,243,LV_INDEV_STATE_REL); elapse(300);
    assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && saved_light_mode==0);
    assert(chronvs_app_open("watch"));
    touch(206,20,LV_INDEV_STATE_PR); touch(206,230,LV_INDEV_STATE_PR);
    touch(206,230,LV_INDEV_STATE_REL); elapse(300);
    capture("quick-settings-lighting-mode");
    touch(249,134,LV_INDEV_STATE_PR); touch(249,99,LV_INDEV_STATE_PR);
    touch(249,99,LV_INDEV_STATE_REL); elapse(300);
    assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && saved_light_mode==0);
    assert(chronvs_app_open("watch"));
    touch(206,20,LV_INDEV_STATE_PR); touch(206,230,LV_INDEV_STATE_PR);
    touch(206,230,LV_INDEV_STATE_REL); elapse(300);
#endif
    assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && lv_obj_get_y(panel)==0);
    /* Closing from the volume button must not change its level on release. */
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(panel, 0);
    touch(292,206,LV_INDEV_STATE_PR); touch(292,170,LV_INDEV_STATE_PR);
    touch(292,70,LV_INDEV_STATE_PR); touch(292,70,LV_INDEV_STATE_REL); elapse(300);
    assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && weather_queries==1);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN); lv_obj_set_y(panel, 0);
    touch(120,206,LV_INDEV_STATE_PR); touch(120,170,LV_INDEV_STATE_PR);
    touch(120,70,LV_INDEV_STATE_PR); touch(120,70,LV_INDEV_STATE_REL); elapse(300);
    assert(sound_volume == 4 && saved_volume == 4);
    assert(previews == 6);
    /* Open the real panel, then tap the empty label, cached icon and cached value. */
    const int weather_tap_y[]={206,194,223};
    for (unsigned i=0;i<3;++i) {
        assert(chronvs_app_open("watch"));
        chronvs_system_ui_notify_activity();
        saved_weather=(chronvs_weather_snapshot_t){.valid=i!=0,.temperature_c=23,.weather_code=2};
        weather_state=CHRONVS_WEATHER_IDLE;
        unsigned queries_before=weather_queries;
        touch(206,30,LV_INDEV_STATE_PR); touch(206,100,LV_INDEV_STATE_PR);
        touch(206,180,LV_INDEV_STATE_PR); touch(206,180,LV_INDEV_STATE_REL); elapse(300);
        assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
        assert(weather_queries==queries_before); /* Viewing remains cache-only. */
        tap(292,weather_tap_y[i]); elapse(300);
        assert(!strcmp(chronvs_app_active_id(),"weather"));
        assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
        assert(weather_queries==queries_before+1); /* Only on_show requests data. */
    }
    assert(chronvs_app_open("gps"));
    assert(gps_requested && gps_snapshot.running);
    assert(find_label_text(chronvs_app_content_layer(), "Procurando módulo"));
    capture("gps-searching");
    gps_snapshot = (chronvs_gps_snapshot_t){.running=true,.serial_fresh=true,.baud=9600,
        .data={.fix=true,.latitude=-23.55052,.longitude=-46.633308,.satellites=8,
            .satellites_valid=true,.speed_valid=true,.speed_kmh=3.2,
            .altitude_valid=true,.altitude_m=760,.hdop_valid=true,.hdop=0.9,.sentences=123,
            .sky={{.talker="GP",.visible=8,.cno=34,.valid=true,.cno_valid=true},
                  {.talker="BD",.visible=4,.cno=36,.valid=true,.cno_valid=true}}}};
    elapse(1100);
    assert(find_label_text(chronvs_app_content_layer(), "-23.550520°"));
    assert(find_label_text(chronvs_app_content_layer(), "GSV GP:8 BD:4"));
    assert(find_label_text(chronvs_app_content_layer(), "HDOP 0.9 / C/N0 36 dBHz"));
    capture("gps-position");
    touch(206,250,LV_INDEV_STATE_PR); touch(206,150,LV_INDEV_STATE_PR);
    touch(206,150,LV_INDEV_STATE_REL); elapse(70);
    assert(find_label_text(chronvs_app_content_layer(),"Atualizar"));
    capture("gps-assisted-ready");
    unsigned assists=assist_requests; tap(206,337); assert(assist_requests==assists+1);
    assisted=(chronvs_agnss_snapshot_t){.state=AGNSS_SENDING,.message="Enviando ao GPS",.sent=12,.total=34,
        .source="Previsão do SD",.gps_accepted=8,.gps_rejected=2,.retries=4,.current_satellite=12,.current_attempt=3};
    elapse(1100); capture("gps-assisted-sending");
    tap(206,337); assert(assist_requests==assists+1);
    assisted=(chronvs_agnss_snapshot_t){.state=AGNSS_ERROR,.message="GPS rejeitou todas as órbitas",.source="IGS do SD",.gps_rejected=32,.retries=64};
    elapse(1100);capture("gps-assisted-rejected");
    assisted=(chronvs_agnss_snapshot_t){.state=AGNSS_SENT,.message="Envio parcial; aguardando GPS",.source="IGS do SD",
        .gps_accepted=1,.gps_rejected=31,.retries=62,.gps_confirmed_total=21};
    elapse(1100);capture("gps-assisted-partial");
    assisted=(chronvs_agnss_snapshot_t){.state=AGNSS_SENT,.message="Enviada; aguardando satélites",.sent=34,.total=34,
        .source="IGS do SD",.gps_accepted=32,.receiver_status=true,.receiver_ephemerides=32};
    elapse(1100); capture("gps-assisted-sent");
#ifdef CHRONVS_GPS_STANDBY_TEST
    touch(206,250,LV_INDEV_STATE_PR); touch(206,150,LV_INDEV_STATE_PR);
    touch(206,150,LV_INDEV_STATE_REL); elapse(70);
    capture("gps-standby-ready");
    tap(140,341); assert(gps_queries == 1 && gps_tests == 0);
    tap(272,341); assert(gps_tests == 1);
    assert(find_label_text(chronvs_app_content_layer(), "Observando resposta..."));
    capture("gps-standby-observing");
    tap(272,341); assert(gps_tests == 1); /* Busy control is disabled. */
    gps_snapshot.test = GPS_TEST_SILENT; gps_snapshot.remaining_s = 40;
    elapse(1100); capture("gps-standby-silent");
    gps_snapshot.test = GPS_TEST_RETURNED;
    elapse(1100); capture("gps-standby-returned");
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    assert(!strcmp(chronvs_app_active_id(), "gps") && gps_tests == 1);
#endif
    unsigned requests_before_back=assist_requests;
    touch(100,337,LV_INDEV_STATE_PR); touch(200,337,LV_INDEV_STATE_PR);
    touch(200,337,LV_INDEV_STATE_REL); elapse(70); /* Back crossing Atualizar never requests assistance. */
    assert(assist_requests==requests_before_back);
    unsigned gps_refreshes = refresh_count;
    elapse(2100); assert(refresh_count == gps_refreshes);
    touch(206,206,LV_INDEV_STATE_PR); elapse(20000);
    assert(!chronvs_system_ui_display_is_off());
    touch(206,206,LV_INDEV_STATE_REL);
    elapse(16000); assert(chronvs_system_ui_display_is_off());
    unsigned gps_off_refreshes = refresh_count;
    gps_snapshot.data.latitude = 10;
    elapse(2100); assert(refresh_count == gps_off_refreshes);
    tap(206,206); elapse(1100);
    assert(!chronvs_system_ui_display_is_off() && !strcmp(chronvs_app_active_id(), "gps"));
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    assert(!strcmp(chronvs_app_active_id(), "apps") && !gps_requested);
    assert(chronvs_app_open("run"));
    assert(gps_recording_requested);
    capture("run-ready");
    gps_snapshot.running = true; gps_snapshot.data.fix = true;
    gps_snapshot.data.utc_valid = true; gps_snapshot.data.utc = 1794054920LL;
    gps_snapshot.data.utc_ms = (uint64_t)now_us/1000;
    chronvs_run_poll(); elapse(1100);
    tap(206,335);
    chronvs_run_snapshot_t run; chronvs_run_snapshot(&run);
    assert(run.state == RUN_RECORDING && run.points == 1);
    now_us += 1000000; ++gps_snapshot.data.utc;
    gps_snapshot.data.utc_ms = (uint64_t)now_us/1000;
    gps_snapshot.data.latitude += 0.000025;
    chronvs_run_poll(); elapse(1100);
    chronvs_run_snapshot(&run); assert(run.points == 2 && run.distance_m > 2);
    capture("run-recording");
    /* Back swipe over the pause action must not activate it or stop recording. */
    touch(100,335,LV_INDEV_STATE_PR); touch(200,335,LV_INDEV_STATE_PR);
    touch(200,335,LV_INDEV_STATE_REL); elapse(70);
    chronvs_run_snapshot(&run);
    assert(!strcmp(chronvs_app_active_id(), "apps") && run.state == RUN_RECORDING && gps_recording_requested);
    assert(chronvs_app_open("gps"));
    assert(chronvs_app_open("run")); assert(gps_recording_requested);
    tap(140,335); chronvs_run_snapshot(&run); assert(run.state == RUN_PAUSED);
    capture("run-paused");
    tap(272,335); capture("run-finish-confirm");
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    chronvs_run_snapshot(&run); assert(run.state == RUN_PAUSED);
    tap(140,335); chronvs_run_snapshot(&run); assert(run.state == RUN_RECORDING);
    elapse(20000); assert(chronvs_system_ui_display_is_off());
    chronvs_gps_poll(true); assert(gps_snapshot.running && gps_recording_requested);
    unsigned run_off_refreshes = refresh_count;
    elapse(2100); assert(refresh_count == run_off_refreshes);
    tap(140,335); chronvs_run_snapshot(&run);
    assert(!chronvs_system_ui_display_is_off() && run.state == RUN_RECORDING);
    tap(272,335); tap(206,307);
    chronvs_run_snapshot(&run); assert(run.state == RUN_SAVED && !gps_recording_requested);
    capture("run-saved");
    touch(206,250,LV_INDEV_STATE_PR); touch(206,150,LV_INDEV_STATE_PR);
    touch(206,150,LV_INDEV_STATE_REL); elapse(70); capture("run-history");
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    assert(chronvs_app_open("Calendario")); capture("25-Calendario-integrated");
    assert(find_label_text(chronvs_app_content_layer(),"Setembro"));
    lv_mem_monitor(&memory); assert(memory.free_biggest_size > 16384);
    printf("Including Calendario: %u bytes free, largest block %u.\n",
        (unsigned)memory.free_size, (unsigned)memory.free_biggest_size);
    elapse(16000); assert(LCD_Backlight==0);
    rtc_before=weather_rtc_reads;
    elapse(60000); assert(weather_rtc_reads==rtc_before);
    /* The entire first vertical swipe only wakes the display. */
    touch(206,270,LV_INDEV_STATE_PR); touch(206,220,LV_INDEV_STATE_PR);
    touch(206,170,LV_INDEV_STATE_PR); touch(206,170,LV_INDEV_STATE_REL);
    assert(find_label_text(chronvs_app_content_layer(),"Setembro"));
    touch(206,270,LV_INDEV_STATE_PR); touch(206,220,LV_INDEV_STATE_PR);
    touch(206,170,LV_INDEV_STATE_PR); touch(206,170,LV_INDEV_STATE_REL);
    assert(find_label_text(chronvs_app_content_layer(),"Outubro"));
    touch(206,170,LV_INDEV_STATE_PR); touch(206,220,LV_INDEV_STATE_PR);
    touch(206,270,LV_INDEV_STATE_PR); touch(206,270,LV_INDEV_STATE_REL);
    assert(find_label_text(chronvs_app_content_layer(),"Setembro"));
    touch(86,88,LV_INDEV_STATE_PR); touch(135,88,LV_INDEV_STATE_PR);
    touch(185,88,LV_INDEV_STATE_PR); touch(185,88,LV_INDEV_STATE_REL);
    assert(!strcmp(chronvs_app_active_id(),"apps"));
    puts("System UI passed: controls, Notas typing/hold, Calendario, AUTO/ECO inactivity and wake-only touch.");
    assert(chronvs_app_open("Calendario"));
    /* Reopening an app does not reorder retained roots. */
    lv_obj_t *Calendario=NULL;
    for (unsigned i=0; i<lv_obj_get_child_cnt(chronvs_app_content_layer()); ++i) {
        lv_obj_t *child=lv_obj_get_child(chronvs_app_content_layer(),i);
        if (!lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN)) { assert(!Calendario); Calendario=child; }
    }
    assert(Calendario);
    tap(290,197); tap(206,357); tap(206,341);
    assert(find_label_text(Calendario,"Título do lembrete"));
    capture("27-Calendario-editor-integrated");
    lv_mem_monitor(&memory);
    printf("Including reminder editor: %u bytes free, largest block %u.\n",
        (unsigned)memory.free_size,(unsigned)memory.free_biggest_size);
    assert(memory.free_biggest_size>8192);
    chronvs_Relogio_init(); set_time(26,9,10,12,0,0);
    chronvs_reminder_t reminder={.year=26,.month=9,.day=10,.hour=12,.minute=1,.title="Aviso durante a edição"};
    assert(chronvs_reminder_create(&reminder)); advance(60); chronvs_Relogio_alert_poll();
    assert(reminder_ringing);
    assert(find_label_text(lv_layer_top(),"Concluir")); capture("28-Calendario-alert-integrated");
    lv_mem_monitor(&memory); assert(memory.free_biggest_size>4096);
    printf("Editor plus alert: %u bytes free, largest block %u.\n",
        (unsigned)memory.free_size,(unsigned)memory.free_biggest_size);
    tap(206,307); chronvs_Relogio_alert_poll(); assert(!reminder_ringing && chronvs_reminder_get(0)->done);
    assert(weather_rtc_reads==0); /* Apps use the shared clock, never I2C. */
#ifdef CHRONVS_PANEL_PROFILE
    /* Test incremental final composition independently of a forced full redraw. */
    assert(chronvs_app_open("watch"));
    chronvs_system_ui_notify_activity(); elapse(300);
    for (unsigned scenario=0;scenario<3;++scenario) {
        touch(206,30,LV_INDEV_STATE_PR);
        touch(206,scenario ? 180 : 80,LV_INDEV_STATE_PR);
        uint64_t before_close = flushed_pixels;
        touch(206,scenario ? 180 : 80,LV_INDEV_STATE_REL); elapse(300);
        if (scenario) {
            assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
            before_close = flushed_pixels;
            if (scenario==2) {
#ifdef CHRONVS_BRIGHTNESS_ARC_REFERENCE
                touch(20,206,LV_INDEV_STATE_PR);
                touch(40,130,LV_INDEV_STATE_PR);
                touch(80,75,LV_INDEV_STATE_PR);
                touch(80,75,LV_INDEV_STATE_REL); elapse(300);
#elif defined(CHRONVS_BRIGHTNESS_SLIDER_REFERENCE)
                touch(120,73,LV_INDEV_STATE_PR);
                touch(230,73,LV_INDEV_STATE_PR);
                touch(230,30,LV_INDEV_STATE_PR);
                touch(230,30,LV_INDEV_STATE_REL); elapse(300);
#else
                tap(163,278); elapse(100); /* Cycle mode without moving the panel. */
#endif
                assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN) && lv_obj_get_y(panel)==0);
            }
            const int x = 120; /* Close outside the arc, also after brightness adjustment. */
            const int y = 206;
            touch(x,y,LV_INDEV_STATE_PR);
            touch(x,y-60,LV_INDEV_STATE_PR);
            touch(x,y-120,LV_INDEV_STATE_PR);
            touch(x,y-120,LV_INDEV_STATE_REL); elapse(300);
        }
        assert(lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));
        printf("Panel close scenario %u: %llu flushed pixels.\n",scenario,
               (unsigned long long)(flushed_pixels-before_close));
        unsigned char *incremental = malloc(sizeof(pixels)); assert(incremental);
        memcpy(incremental,pixels,sizeof(pixels));
        lv_obj_invalidate(lv_scr_act()); lv_refr_now(NULL);
        assert(!memcmp(incremental,pixels,sizeof(pixels)));
        free(incremental);
    }
    assert(panels[CHRONVS_PANEL_QUICK].slices > 0);
    assert(panels[CHRONVS_PANEL_LAUNCHER].slices > 0);
#endif
    assert(watch_frames > 0 && watch_slices > watch_frames);
    chronvs_display_profile_poll(true);
    assert(frames==0 && touch_reads==0 && pending_input_us==0);
    assert(watch_frames==0 && watch_slices==0);
#ifdef CHRONVS_PANEL_PROFILE
    for(unsigned i=0;i<CHRONVS_PANEL_COUNT;++i)
        assert(!panels[i].frames && !panels[i].slices && !panels[i].draw_us);
    /* Controlled clock: nested root/content time and parent-only POST events. */
    lv_obj_t *probe=lv_obj_create(lv_layer_top());
    chronvs_display_profile_bind_panel(probe,CHRONVS_PANEL_QUICK);
    lv_area_t probe_clip={10,20,29,39};
    lv_draw_ctx_t probe_ctx={.clip_area=&probe_clip};
    lv_event_send(probe,LV_EVENT_DRAW_POST_END,&probe_ctx);
    lv_event_send(probe,LV_EVENT_DRAW_MAIN_BEGIN,&probe_ctx);
    now_us+=100; lv_event_send(probe,LV_EVENT_DRAW_MAIN_END,&probe_ctx);
    now_us+=250; lv_event_send(probe,LV_EVENT_DRAW_POST_END,&probe_ctx);
    profile_monitor(NULL,1,400);
    assert(panels[0].frames==1 && panels[0].slices==1 && panels[0].clip_pixels==400);
    assert(panels[0].root_us==100 && panels[0].draw_us==350 && panels[0].post_only==1);
    chronvs_display_profile_poll(true);
    assert(!panels[0].started && !panels[0].frames && !panels[0].post_only);
    lv_obj_del(probe);
#endif
    int64_t profile_test_start = chronvs_display_profile_watch_begin();
    now_us += 100;
    profile_test_start = chronvs_display_profile_watch_mark(CHRONVS_WATCH_SETUP, profile_test_start);
    now_us += 200;
    chronvs_display_profile_watch_mark(CHRONVS_WATCH_RINGS, profile_test_start);
    profile_test_start = chronvs_display_profile_watch_begin();
    now_us += 50;
    chronvs_display_profile_watch_mark(CHRONVS_WATCH_DATES, profile_test_start);
    profile_monitor(NULL, 1, 1);
    assert(watch_frames==1 && watch_slices==2);
    assert(watch_us[CHRONVS_WATCH_SETUP]==100 && watch_us[CHRONVS_WATCH_RINGS]==200);
    assert(watch_us[CHRONVS_WATCH_DATES]==50);
    chronvs_display_profile_poll(true);
    assert(watch_frames==0 && watch_slices==0 && !watch_in_refresh);
    for (unsigned i=0;i<CHRONVS_WATCH_SECTION_COUNT;++i) assert(watch_us[i]==0);
    /* A held, stationary finger can leave a long interval without rendering.
     * Only coordinate changes during contact count as motion. */
    lv_indev_data_t sample = {0};
    contact = LV_INDEV_STATE_PR; point = (lv_point_t){100,100};
    profile_read(NULL, &sample);
    assert(motion_reads == 0);
    unsigned starts_before = render_start_count;
    profile_render_start(NULL); now_us += 5000; profile_monitor(NULL, 5, 1);
    now_us += 800000;
    profile_read(NULL, &sample);
    assert(motion_reads == 0 && latest_motion_us == 0);
    profile_render_start(NULL); now_us += 4000; profile_monitor(NULL, 4, 1);
    assert(render_idle_max_us == 800000 && render_max_us == 5000 && motion_frames == 0);
    now_us += 100; point.x += 10; profile_read(NULL, &sample);
    now_us += 200; point.x += 10; profile_read(NULL, &sample);
    now_us += 300; contact = LV_INDEV_STATE_REL; profile_read(NULL, &sample);
    profile_render_start(NULL); now_us += 6000; profile_monitor(NULL, 6, 1);
    assert(motion_reads == 2 && motion_frames == 1 && motion_age_max_us == 300);
    assert(render_max_us == 6000 && render_start_count == starts_before + 3);
    chronvs_display_profile_poll(true);
    assert(render_started_us == 0 && render_finished_us == 0 && latest_motion_us == 0);
    assert(render_max_us == 0 && render_idle_max_us == 0 && motion_frames == 0 && motion_reads == 0);
    assert(motion_age_max_us == 0 && !render_had_motion);
#ifdef CHRONVS_LAUNCHER_ICON_CACHE
    for (unsigned i = 0; i < lv_obj_get_child_cnt(app_list); ++i) {
        lv_obj_t *badge = lv_obj_get_child(lv_obj_get_child(app_list, i), 0);
        if (lv_obj_get_child_cnt(badge) == 2) {
            lv_obj_t *image = lv_obj_get_child(badge, 1);
            const lv_img_dsc_t *descriptor = lv_img_get_src(image);
            assert(descriptor->data != NULL);
            lv_obj_del(image);
            assert(descriptor->data == NULL);
        }
    }
#endif
    test_power_menu();
#ifdef CHRONVS_PANEL_AUTOTEST
    assert(chronvs_app_open("watch"));
    chronvs_panel_autotest_init();
    bool quick_moving=false, quick_open=false, launcher_moving=false, launcher_open=false;
    for(unsigned ms=0;ms<110000 && chronvs_panel_autotest_status().running;ms+=35) {
        elapse(35);
        chronvs_panel_test_status_t test=chronvs_panel_autotest_status();
        chronvs_ui_panel_probe_t probe=chronvs_system_ui_panel_probe();
        if (!quick_moving && test.completed==0 && !probe.hidden && probe.y>-260 && probe.y<-160) {
            save_current_pixels("panel-auto-quick-moving"); quick_moving=true;
        }
        if (!quick_open && test.completed==1) { save_current_pixels("panel-auto-quick-open"); quick_open=true; }
        if (test.completed==3) for(unsigned i=0;i<lv_obj_get_child_cnt(chronvs_app_content_layer());++i) {
            lv_obj_t *root=lv_obj_get_child(chronvs_app_content_layer(),i);
            if (!launcher_moving && !lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN) &&
                lv_obj_get_style_radius(root,0)==LV_RADIUS_CIRCLE && lv_obj_get_y(root)>160 && lv_obj_get_y(root)<260) {
                save_current_pixels("panel-auto-launcher-moving"); launcher_moving=true;
            }
        }
        if (!launcher_open && test.completed==4) { save_current_pixels("panel-auto-launcher-open"); launcher_open=true; }
    }
    chronvs_panel_test_status_t test=chronvs_panel_autotest_status();
    printf("Panel automation: completed=%u total=%u failed=%u\n",test.completed,test.total,test.failed);
    assert(!test.running && test.completed==42 && !test.failed);
    assert(quick_moving && quick_open && launcher_moving && launcher_open);
    assert(!strcmp(chronvs_app_active_id(),"watch"));
#endif
#ifdef CHRONVS_DISPLAY_BENCH
    assert(chronvs_app_open("watch"));
    unsigned bench_children = lv_obj_get_child_cnt(lv_scr_act());
    chronvs_display_bench_init(); chronvs_display_bench_init();
    assert(lv_obj_get_child_cnt(lv_scr_act()) == bench_children + 1);
    lv_obj_t *bench = lv_obj_get_child(lv_scr_act(), -1);
    assert(lv_obj_get_style_bg_opa(bench, 0) == LV_OPA_COVER);
    capture("display-bench-flat");
    tap(206,180);
    assert(lv_obj_get_style_bg_opa(bench, 0) == LV_OPA_COVER);
    elapse(8100);
    assert(lv_obj_get_style_bg_opa(bench, 0) == LV_OPA_TRANSP);
    capture("display-bench-watch");
#ifdef CHRONVS_DISPLAY_BENCH_AUTORUN
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    assert(lv_obj_get_child_cnt(lv_scr_act()) == bench_children + 1);
    /* Real held contact keeps AUTO awake without allowing navigation. */
    touch(206,180,LV_INDEV_STATE_PR);
    elapse(33000);
    touch(206,180,LV_INDEV_STATE_REL);
    assert(lv_obj_get_child_cnt(lv_scr_act()) == bench_children);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    puts("Display bench autorun: touch does not navigate; the bounded test exits automatically.");
#else
    /* Animation must not count as interaction or render while off. */
    elapse(130000);
    assert(chronvs_system_ui_display_is_off());
    unsigned bench_off_refreshes = refresh_count;
    elapse(1000); assert(refresh_count == bench_off_refreshes);
    const lv_opa_t bench_before_wake = lv_obj_get_style_bg_opa(bench, 0);
    tap(206,180);
    assert(!chronvs_system_ui_display_is_off());
    assert(lv_obj_get_style_bg_opa(bench, 0) == bench_before_wake);
    touch(100,180,LV_INDEV_STATE_PR); touch(200,180,LV_INDEV_STATE_PR);
    touch(200,180,LV_INDEV_STATE_REL); elapse(70);
    assert(lv_obj_get_child_cnt(lv_scr_act()) == bench_children);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    puts("Display bench: background switch, inactivity, off-screen pause, wake-only tap and consumed exit swipe passed.");
#endif
#endif
    test_launcher_background_cache();
    return 0;
}
