#include "platform/panel_autotest.h"
#ifdef CHRONVS_LAUNCHER_SCROLL_AUTOTEST
#include <inttypes.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "core/app_manager.h"
#include "apps/app_list_app.h"
#include "ui/circle_fill.h"
#include "ui/system_ui.h"

typedef struct { const char *name; int from, to; unsigned duration, hold; bool reverse; } path_t;
static const path_t paths[]={
    {"slow_up",310,100,900,0,false}, {"fast_up",340,70,180,0,false},
    {"hold_up",310,100,250,700,false}, {"reverse_up",300,100,700,0,true},
    {"slow_down",100,310,900,0,false}, {"fast_down",70,340,180,0,false},
    {"hold_down",100,310,250,700,false}, {"reverse_down",100,300,700,0,true},
};
enum { CASES=sizeof(paths)/sizeof(paths[0]), TOTAL=CASES*3 };
static lv_indev_t *pointer;
static lv_disp_drv_t *display;
static void (*old_read)(lv_indev_drv_t *,lv_indev_data_t *);
static void (*old_flush)(lv_disp_drv_t *,const lv_area_t *,lv_color_t *);
static void (*old_start)(lv_disp_drv_t *);
static void (*old_monitor)(lv_disp_drv_t *,uint32_t,uint32_t);
static lv_obj_t *root, *list;
static bool running, begun, waking, pressed, endpoint, released, geometry_ok, idle_check;
static unsigned completed, failed, rows, frames, slices, moves, gap_max, frame_max;
static unsigned wraps_up, wraps_down, idle_frames;
static uint32_t idle_tick;
static uint32_t startup, began_tick, press_tick, release_tick, previous_read_tick;
static int cycle, previous_scroll, release_scroll, travel;
static uint64_t render_us, flush_us, frame_flush, pixels;
static int64_t frame_tick;
static size_t internal_min, dma_min, lvgl_min, largest_min;
static chronvs_ui_panel_probe_t preferences;
static const struct { const char *name; unsigned mode; } cost_modes[]={
    {"full_reference",CHRONVS_SCROLL_REFERENCE_FILL},
    {"full",0}, {"no_icons",CHRONVS_SCROLL_NO_ICONS},
    {"no_labels",CHRONVS_SCROLL_NO_LABELS}, {"no_badges",CHRONVS_SCROLL_NO_BADGES},
    {"no_fade",CHRONVS_SCROLL_NO_FADE}, {"no_clip",CHRONVS_SCROLL_NO_CLIP},
    {"background",CHRONVS_SCROLL_NO_BADGES|CHRONVS_SCROLL_NO_LABELS},
    {"square_background",CHRONVS_SCROLL_NO_BADGES|CHRONVS_SCROLL_NO_LABELS|CHRONVS_SCROLL_NO_CLIP|CHRONVS_SCROLL_SQUARE},
    {"full_check",0},
};
enum { COST_MODES=sizeof(cost_modes)/sizeof(cost_modes[0]), COST_FRAMES=32, COST_REPEATS=3 };
static bool cost_running, cost_measuring;
static unsigned cost_group;
static int cost_frame=-1;
static uint64_t cost_prepare_us;
static uint32_t cost_cache_hits;

static bool measure_active(void) {
    return running && begun && !waking && (!cost_running || cost_measuring);
}

static void finish(bool failure) {
    failed+=failure;
    cost_measuring=false;
    if (list) chronvs_app_list_cost_frame(0,lv_obj_get_scroll_y(list)%cycle);
    running=false;
    pointer->driver->read_cb=old_read;
    display->flush_cb=old_flush; display->render_start_cb=old_start; display->monitor_cb=old_monitor;
    lv_indev_reset(pointer,NULL); lv_indev_wait_release(pointer);
    chronvs_app_open("watch");
    ESP_LOGI("scroll_test","DONE completed=%u total=%u failed=%u wraps_up=%u wraps_down=%u physical_touch_restored=1",completed,TOTAL,failed,wraps_up,wraps_down);
}

static void read_script(lv_indev_drv_t *drv,lv_indev_data_t *data) {
    (void)drv;
    data->state=LV_INDEV_STATE_REL; data->point=(lv_point_t){210,206};
    /* A stationary scripted contact in the non-scroll top strip keeps normal
     * inactivity handling active during controlled rendering. No preferences
     * are changed, and this path does not exist in the standard firmware. */
    if (cost_running && running) { data->state=LV_INDEV_STATE_PR; data->point.y=30; return; }
    if (!running || !begun || released) return;
    if (!chronvs_system_ui_display_is_off() && lv_obj_get_child_cnt(lv_layer_top())) return;
    const uint32_t now=lv_tick_get();
    const path_t *p=&paths[completed%CASES];
    data->point.y=waking ? 206 : p->reverse ? p->from : p->to;
    if (endpoint) {
        released=true; release_tick=now;
        if (!waking) release_scroll=lv_obj_get_scroll_y(list)%cycle;
        return;
    }
    if (!pressed) { pressed=true; press_tick=previous_read_tick=now; }
    unsigned gap=now-previous_read_tick; if (gap>gap_max) gap_max=gap;
    previous_read_tick=now;
    data->state=LV_INDEV_STATE_PR;
    const unsigned held=now-press_tick;
    if (waking) { if (held>=120) endpoint=true; return; }
    unsigned t=held>90 ? held-90 : 0;
    if (t>p->duration) t=p->duration;
    if (p->reverse) t=t<=p->duration/2 ? t*2 : (p->duration-t)*2;
    data->point.y=p->from+(int32_t)(p->to-p->from)*(int32_t)t/(int32_t)p->duration;
    if (held>=90+p->duration+p->hold) endpoint=true;
}
static void start_frame(lv_disp_drv_t *drv) {
    frame_tick=esp_timer_get_time(); frame_flush=0;
    if (old_start) old_start(drv);
}
static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *colors) {
    int64_t start=esp_timer_get_time(); old_flush(drv,area,colors);
    uint64_t duration=esp_timer_get_time()-start; frame_flush+=duration;
    if (measure_active()) { flush_us+=duration; ++slices; }
}
static void monitor(lv_disp_drv_t *drv,uint32_t ms,uint32_t px) {
    uint32_t duration=(uint32_t)(esp_timer_get_time()-frame_tick);
    if (measure_active()) {
        ++frames; pixels+=px;
        if (duration>frame_max) frame_max=duration;
        if (duration>=frame_flush) render_us+=duration-frame_flush;
    }
    if (old_monitor) old_monitor(drv,ms,px);
}
static void cost_poll(void) {
    if (chronvs_system_ui_display_is_off() || lv_obj_get_child_cnt(lv_layer_top())) {
        ESP_LOGE("scroll_test","ABORT controlled render: display off or modal"); finish(true); return;
    }
    const unsigned index=cost_group%COST_MODES;
    const unsigned mode=cost_modes[index].mode;
    if (cost_frame<0) {
        chronvs_app_list_cost_frame(mode,0);
        lv_obj_invalidate(lv_scr_act()); lv_refr_now(NULL); /* Unmeasured warmup. */
        frames=slices=frame_max=0; render_us=flush_us=pixels=cost_prepare_us=0;
        uint32_t bytes;
        chronvs_circle_fill_info(root,&bytes,&cost_cache_hits);
        cost_frame=0;
        return;
    }
    const int64_t start=esp_timer_get_time();
    chronvs_app_list_cost_frame(mode,(cost_frame*23)%cycle);
    cost_prepare_us+=esp_timer_get_time()-start;
    /* Every variant draws exactly the same 32 phases and full-screen pixel
     * area. Thus differences do not come from a faster variant sampling more
     * touch points, or from sending a smaller dirty rectangle. */
    lv_obj_invalidate(lv_scr_act());
    cost_measuring=true; lv_refr_now(NULL); cost_measuring=false;
    if (++cost_frame<COST_FRAMES) return;
    lv_mem_monitor_t mem; lv_mem_monitor(&mem);
    uint32_t cache_bytes,hits;
    const bool cache_valid=chronvs_circle_fill_info(root,&cache_bytes,&hits);
    const bool pass=frames==COST_FRAMES && pixels==(uint64_t)COST_FRAMES*412*412 &&
        !strcmp(chronvs_app_active_id(),"apps") && lv_obj_get_child_cnt(list)==rows;
    ESP_LOGI("scroll_test","COST_RESULT name=%s repeat=%u pass=%u frames=%u px_avg=%" PRIu64 " render_avg_us=%" PRIu64 " flush_avg_us=%" PRIu64 " prepare_avg_us=%" PRIu64 " frame_max_us=%u lvgl_free=%u lvgl_largest=%u bg_valid=%u bg_bytes=%u bg_hits=%u",
        cost_modes[index].name,cost_group/COST_MODES+1,pass,frames,frames ? pixels/frames : 0,
        frames ? render_us/frames : 0,frames ? flush_us/frames : 0,cost_prepare_us/COST_FRAMES,frame_max,
        (unsigned)mem.free_size,(unsigned)mem.free_biggest_size,cache_valid,(unsigned)cache_bytes,(unsigned)(hits-cost_cache_hits));
    ++cost_group; cost_frame=-1;
    if (!pass || cost_group==COST_MODES*COST_REPEATS) {
        ESP_LOGI("scroll_test","COST_DONE groups=%u expected=%u pass=%u",cost_group,COST_MODES*COST_REPEATS,pass);
        finish(!pass); return;
    }
}
static void begin(void) {
    pressed=endpoint=released=false; geometry_ok=true;
    frames=slices=moves=gap_max=frame_max=0; render_us=flush_us=pixels=0; travel=0;
    internal_min=dma_min=lvgl_min=largest_min=SIZE_MAX;
    began_tick=lv_tick_get();
    if (list) previous_scroll=lv_obj_get_scroll_y(list);
    ESP_LOGI("scroll_test","BEGIN n=%u name=%s repeat=%u",completed+1,waking ? "wake" : paths[completed%CASES].name,completed/CASES+1);
}
void chronvs_panel_autotest_init(void) {
    if (pointer) return;
    for (lv_indev_t *p=lv_indev_get_next(NULL);p;p=lv_indev_get_next(p))
        if (lv_indev_get_type(p)==LV_INDEV_TYPE_POINTER) { pointer=p; break; }
    lv_disp_t *disp=lv_disp_get_default();
    if (!pointer || !disp || !disp->driver->flush_cb) return;
    display=disp->driver; old_read=pointer->driver->read_cb; old_flush=display->flush_cb;
    old_start=display->render_start_cb; old_monitor=display->monitor_cb;
    pointer->driver->read_cb=read_script; display->flush_cb=flush;
    display->render_start_cb=start_frame; display->monitor_cb=monitor;
    lv_indev_reset(pointer,NULL); running=true; startup=lv_tick_get();
    ESP_LOGI("scroll_test","READY total=%u startup_ms=8000 real_QSPI=1 touch=scripted",TOTAL);
}
void chronvs_panel_autotest_poll(void) {
    if (!running) return;
    if (cost_running) { cost_poll(); return; }
    if (idle_check) {
        if (lv_tick_elaps(idle_tick)<2000) return;
        const bool pass=frames==idle_frames && !chronvs_system_ui_display_is_off();
        ESP_LOGI("scroll_test","IDLE pass=%u duration_ms=2000 frames=%u",pass,frames-idle_frames);
        if (!pass || !wraps_up || !wraps_down) { finish(true); return; }
        idle_check=false; cost_running=true;
        ESP_LOGI("scroll_test","COST_READY modes=%u repeats=%u frames_per_mode=%u matched_phases=1 full_screen=1",COST_MODES,COST_REPEATS,COST_FRAMES);
        return;
    }
    if (!begun) {
        if (lv_tick_elaps(startup)<8000) return;
        chronvs_app_open("watch"); begun=waking=true; begin(); return;
    }
    if (lv_tick_elaps(began_tick)>7000 ||
        (!waking && (chronvs_system_ui_display_is_off() || lv_obj_get_child_cnt(lv_layer_top())))) {
        ESP_LOGE("scroll_test","ABORT timeout, display off or modal"); finish(true); return;
    }
    if (!waking) {
        int scroll=lv_obj_get_scroll_y(list);
        int delta=(scroll-previous_scroll+cycle+cycle/2)%cycle-cycle/2;
        if (delta>0 && scroll%cycle<previous_scroll%cycle) ++wraps_up;
        if (delta<0 && scroll%cycle>previous_scroll%cycle) ++wraps_down;
        previous_scroll=scroll;
        if (delta) { ++moves; travel+=delta<0 ? -delta : delta; }
        unsigned visible=0;
        lv_obj_update_layout(list);
        for (unsigned i=0;i<rows;++i) {
            lv_obj_t *row=lv_obj_get_child(list,i);
            if (!row) { geometry_ok=false; break; }
            if ((lv_obj_get_y(row)-174-(int)i*82)%cycle) geometry_ok=false;
            lv_area_t bounds; lv_obj_get_coords(row,&bounds);
            if (bounds.y1<312 && bounds.y2>100 && lv_obj_get_style_opa_layered(row,0)) ++visible;
        }
        if (!visible || lv_obj_get_child_cnt(list)!=rows) geometry_ok=false;
        size_t available=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        size_t dma=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
        lv_mem_monitor_t mem; lv_mem_monitor(&mem);
        if (available<internal_min) internal_min=available;
        if (dma<dma_min) dma_min=dma;
        if (mem.free_size<lvgl_min) lvgl_min=mem.free_size;
        if (mem.free_biggest_size<largest_min) largest_min=mem.free_biggest_size;
    }
    if (!released || lv_tick_elaps(release_tick)<650) return;
    if (waking) {
        waking=false; preferences=chronvs_system_ui_panel_probe();
        if (!chronvs_app_open("apps")) { finish(true); return; }
        lv_obj_t *layer=chronvs_app_content_layer();
        for (unsigned i=0;i<lv_obj_get_child_cnt(layer);++i) {
            lv_obj_t *candidate=lv_obj_get_child(layer,i);
            if (lv_obj_get_style_radius(candidate,0)==LV_RADIUS_CIRCLE) root=candidate;
        }
        if (!root || !(list=lv_obj_get_child(root,0))) { finish(true); return; }
        rows=lv_obj_get_child_cnt(list); cycle=(int)rows*82;
        if (!cycle) { finish(true); return; }
        ESP_LOGI("scroll_test","CATALOG rows=%u cycle=%d",rows,cycle);
        begin(); return;
    }
    const chronvs_ui_panel_probe_t probe=chronvs_system_ui_panel_probe();
    const bool prefs=probe.brightness==preferences.brightness && probe.profile==preferences.profile && probe.eco==preferences.eco;
    int coast=(lv_obj_get_scroll_y(list)%cycle-release_scroll+cycle)%cycle;
    bool pass=geometry_ok && prefs && moves && travel>20 && frames && !coast &&
        !lv_obj_is_scrolling(list) && !strcmp(chronvs_app_active_id(),"apps") && lv_obj_get_y(root)==0;
    ESP_LOGI("scroll_test","RESULT n=%u name=%s repeat=%u pass=%u rows=%u travel=%d coast=%d geometry=%u prefs=%u frames=%u slices=%u px_avg=%" PRIu64 " render_avg_us=%" PRIu64 " flush_avg_us=%" PRIu64 " frame_max_us=%u touch_gap_max_ms=%u internal_min=%u dma_min=%u lvgl_min=%u lvgl_largest_min=%u",
        completed+1,paths[completed%CASES].name,completed/CASES+1,pass,rows,travel,coast,geometry_ok,prefs,frames,slices,
        frames ? pixels/frames : 0,frames ? render_us/frames : 0,frames ? flush_us/frames : 0,frame_max,gap_max,
        (unsigned)internal_min,(unsigned)dma_min,(unsigned)lvgl_min,(unsigned)largest_min);
    ++completed;
    if (!pass) { finish(true); return; }
    if (completed==TOTAL) { idle_check=true; idle_tick=lv_tick_get(); idle_frames=frames; return; }
    begin();
}
chronvs_panel_test_status_t chronvs_panel_autotest_status(void) {
    return (chronvs_panel_test_status_t){running,completed,failed,TOTAL};
}
#endif
