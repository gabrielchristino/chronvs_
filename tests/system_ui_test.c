/* Render the real watch, launcher and global controls with hardware stubs. */
#define main service_test_main
#define nvs_set_blob aion_test_set_blob
#include "aion_service_test.c"
#undef main
#undef nvs_set_blob
#include "lvgl.h"
#include "apps/app_catalog.h"
#include "ui/system_ui.h"
#include "ui/control_style.h"
#include "esp_heap_caps.h"
#include "services/mnemo_service.h"
#include "services/sound_service.h"
#include "services/weather_service.h"
#include "platform/lvgl_memory.h"
#include <stdlib.h>

int nvs_set_blob(nvs_handle_t h, const char *k, const void *in, size_t size) {
    if (!strncmp(k,"note",4)) {
        assert(size==sizeof(mnemo_note_t)); return fail_save ? -1 : 0;
    }
    return aion_test_set_blob(h,k,in,size);
}

static lv_indev_state_t contact;
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
        lv_tick_inc(step); lv_timer_handler(); ms-=step;
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
    return calloc(count, size);
}
static unsigned lvgl_pool_allocations;
void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(size==LV_MEM_SIZE && caps==(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++lvgl_pool_allocations;
    return malloc(size);
}

uint8_t LCD_Backlight;
void Set_Backlight(uint8_t brightness) { LCD_Backlight=brightness; }
int nvs_flash_init(void) { return 0; }
int nvs_flash_erase(void) { return 0; }
static uint8_t sound_volume = 1, saved_volume = 4;
static unsigned previews;
static uint8_t preview_volume;
void chronvs_sound_preview(void) { ++previews; preview_volume = sound_volume; }
uint8_t chronvs_sound_volume(void) { return sound_volume; }
void chronvs_sound_set_volume(uint8_t level) { assert(level <= 5); sound_volume = level; }
int nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) {
    (void)h;
    if (!strcmp(k, "volume")) { *v = saved_volume; return 0; }
    return -1;
}
int nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) {
    (void)h;
    if (!strcmp(k, "volume")) saved_volume = v;
    return 0;
}

static unsigned char pixels[412*412*3];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    for(int y=area->y1;y<=area->y2;++y) for(int x=area->x1;x<=area->x2;++x) {
        lv_color32_t c={.full=lv_color_to32(*colors++)};
        unsigned char *p=&pixels[3*((411-y)*412+x)];
        if((x-206)*(x-206)+(y-206)*(y-206)>206*206) p[0]=p[1]=p[2]=0;
        else {p[0]=c.ch.blue;p[1]=c.ch.green;p[2]=c.ch.red;}
    }
    lv_disp_flush_ready(driver);
}
static void capture(const char *name) {
    lv_obj_update_layout(lv_scr_act()); lv_tick_inc(100);lv_timer_handler();lv_refr_now(NULL);
    char path[160];snprintf(path,sizeof(path),".pio/host-tests/%s.bmp",name);
    FILE *f=fopen(path,"wb");assert(f);
    unsigned char h[54]={'B','M'};
    uint32_t size=sizeof(pixels)+54,offset=54,dib=40,dim=412;uint16_t planes=1,bits=24;
    memcpy(h+2,&size,4);memcpy(h+10,&offset,4);memcpy(h+14,&dib,4);
    memcpy(h+18,&dim,4);memcpy(h+22,&dim,4);memcpy(h+26,&planes,2);memcpy(h+28,&bits,2);
    fwrite(h,1,54,f);fwrite(pixels,1,sizeof(pixels),f);fclose(f);
}
int main(void) {
    lv_init();
    assert(lvgl_pool_allocations==1);
    assert(chronvs_lvgl_pool_alloc(LV_MEM_SIZE)==chronvs_lvgl_pool_alloc(LV_MEM_SIZE));
    assert(lvgl_pool_allocations==1); /* Reinitialization keeps the same arena. */
    static lv_color_t buffer[412*412/20];static lv_disp_draw_buf_t draw;static lv_disp_drv_t driver;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);lv_disp_drv_init(&driver);
    driver.hor_res=driver.ver_res=412;driver.draw_buf=&draw;driver.flush_cb=flush;
    lv_disp_drv_register(&driver);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER;input.read_cb=read_touch;lv_indev_drv_register(&input);
    chronvs_app_manager_init(lv_scr_act());assert(chronvs_apps_register_all());
    assert(chronvs_app_open("watch"));capture("12-watch");
    lv_obj_t *panel=lv_obj_get_child(lv_scr_act(),1);assert(panel);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(panel,0);
    chronvs_system_ui_set_battery(72,3.9f);capture("13-quick-settings");
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
    assert(sound_volume == 4 && LCD_Backlight == 70); /* Wake without opening Clima. */
    assert(previews == 6);
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    assert(chronvs_app_open("apps"));capture("14-app-list");
    lv_obj_t *launcher = lv_obj_get_child(chronvs_app_content_layer(), -1);
    lv_obj_t *app_list = lv_obj_get_child(launcher, 0);
    assert(lv_obj_get_child_cnt(app_list) == 4);
    lv_obj_t *first_row = lv_obj_get_child(app_list, 0);
    lv_obj_t *middle_row = lv_obj_get_child(app_list, 1);
    assert(lv_obj_get_style_translate_x(first_row, 0) >
           lv_obj_get_style_translate_x(middle_row, 0));
    lv_obj_scroll_to_y(app_list, 0, LV_ANIM_OFF); elapse(70);
    int centered_x = lv_obj_get_style_translate_x(first_row, 0);
    lv_obj_scroll_to_y(app_list, 82, LV_ANIM_OFF); elapse(70);
    assert(lv_obj_get_style_translate_x(first_row, 0) > centered_x);
    capture("19-launcher-arc");
    /* A vertical drag on an app scrolls the arc without launching on release. */
    touch(150,206,LV_INDEV_STATE_PR);
    touch(150,170,LV_INDEV_STATE_PR);
    touch(150,120,LV_INDEV_STATE_PR);
    touch(150,120,LV_INDEV_STATE_REL); elapse(500);
    assert(!strcmp(chronvs_app_active_id(), "apps"));
    assert(lv_obj_get_scroll_y(app_list) > 82);
    capture("20-launcher-scrolled");
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
    assert(!strcmp(chronvs_app_active_id(), "apps") && lv_obj_get_y(launcher) == 0);
    touch(206,30,LV_INDEV_STATE_PR); touch(206,90,LV_INDEV_STATE_PR);
    touch(206,170,LV_INDEV_STATE_PR); touch(206,170,LV_INDEV_STATE_REL); elapse(70);
    assert(!strcmp(chronvs_app_active_id(), "watch"));
    assert(chronvs_app_open("aion"));
    assert(chronvs_app_open("mnemo"));capture("15-mnemo-integrated");
    tap(206,341); /* Nova nota. */
    lv_obj_t *text=find_textarea(chronvs_app_content_layer()); assert(text);
    /* Real power timer: continuous typing exceeds both AUTO deadlines. */
    for(unsigned i=0;i<50;++i) {
        tap(76,226); elapse(1000); assert(LCD_Backlight==70);
    }
    assert(mnemo_text_length(lv_textarea_get_text(text))==50);
    elapse(16000); assert(LCD_Backlight==12);
    tap(150,178); assert(LCD_Backlight==70);
    elapse(46000); assert(chronvs_system_ui_display_is_off() && LCD_Backlight==0);
    unsigned length=mnemo_text_length(lv_textarea_get_text(text));
    tap(150,178); assert(LCD_Backlight==70);
    assert(mnemo_text_length(lv_textarea_get_text(text))==length); /* Wake only. */
    tap(76,226); assert(mnemo_text_length(lv_textarea_get_text(text))==length+1);
    /* Toggle the actual ECO control, then hold a key past its 15 s deadline. */
    lv_obj_t *eco=NULL;
    for(unsigned i=0;i<lv_obj_get_child_cnt(panel);++i) {
        lv_obj_t *obj=lv_obj_get_child(panel,i);
        if(lv_obj_check_type(obj,&lv_btn_class) && lv_obj_get_x(obj)==214 && lv_obj_get_y(obj)==99) eco=obj;
    }
    assert(eco); lv_event_send(eco,LV_EVENT_CLICKED,NULL); assert(LCD_Backlight==35);
    touch(76,226,LV_INDEV_STATE_PR);
    for(unsigned i=0;i<20;++i) { elapse(1000); assert(LCD_Backlight==35); }
    touch(76,226,LV_INDEV_STATE_REL);
    elapse(6000); assert(LCD_Backlight==5);
    elapse(10000); assert(LCD_Backlight==0);
    length=mnemo_text_length(lv_textarea_get_text(text));
    tap(150,178); assert(LCD_Backlight==35);
    assert(mnemo_text_length(lv_textarea_get_text(text))==length);
    capture("16-mnemo-keyboard-integrated");
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
    assert(chronvs_app_open("aion"));
    assert(chronvs_app_open("mnemo"));
    lv_mem_monitor(&memory); assert(memory.free_biggest_size > 16384);
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
    puts("System UI passed: controls, Mnemo typing/hold, AUTO/ECO inactivity and wake-only touch.");
    return 0;
}
