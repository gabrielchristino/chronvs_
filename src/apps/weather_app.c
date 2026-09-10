#include "apps/app_catalog.h"

#include <stdio.h>
#include <string.h>
#include "core/app_manager.h"
#include "services/weather_data.h"
#include "services/weather_service.h"
#include "ui/aion_widgets.h"
#include "ui/app_input.h"
#include "ui/mnemo_font.h"
#include "ui/system_ui.h"
#include "ui/weather_icon.h"

static lv_obj_t *temperature, *condition, *apparent, *humidity, *range, *age, *status, *symbol;
static lv_timer_t *refresh;
static chronvs_weather_snapshot_t snapshot;
static chronvs_weather_state_t shown_state;
static bool visible, dirty, was_off;
static uint32_t age_tick;
static void back(void) { chronvs_app_open("apps"); }
static chronvs_ui_app_input_t input = {.back = back};

static void draw_symbol(lv_event_t *event) {
    const bool launcher = lv_event_get_user_data(event) != NULL;
    if (!launcher && !snapshot.valid) return;
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    chronvs_ui_draw_weather_icon(lv_event_get_draw_ctx(event), &bounds,
        launcher ? WEATHER_PARTLY_CLOUDY : chronvs_weather_icon(snapshot.weather_code));
}

static void create_icon(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, draw_symbol, LV_EVENT_DRAW_MAIN, (void *)1);
}

static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void render(void) {
    char text[80];
    if (snapshot.valid) {
        snprintf(text,sizeof(text),"%.0f°C",snapshot.temperature_c);
        set_text(temperature,text);
        set_text(condition,chronvs_weather_condition(snapshot.weather_code));
        snprintf(text,sizeof(text),"Sensação %.0f°C",snapshot.apparent_temperature_c);
        set_text(apparent,text);
        snprintf(text,sizeof(text),"Umidade %u%%",snapshot.humidity_percent);
        set_text(humidity,text);
        snprintf(text,sizeof(text),"Mín %.0f°   /   Máx %.0f°",snapshot.minimum_c,snapshot.maximum_c);
        set_text(range,text);
    } else {
        set_text(temperature,"--°C");
        set_text(condition,"Sem dados");
        set_text(apparent,""); set_text(humidity,""); set_text(range,"");
    }
    shown_state = chronvs_weather_state();
    set_text(status,shown_state == CHRONVS_WEATHER_FETCHING ? "Atualizando..." :
        shown_state == CHRONVS_WEATHER_ERROR || *chronvs_weather_error() ?
            (snapshot.valid ? "Falha ao atualizar" : chronvs_weather_error()) : "");
    lv_obj_invalidate(symbol);
    dirty = false;
}

static void poll(lv_timer_t *timer) {
    (void)timer;
    if (!visible) return;
    if (chronvs_system_ui_display_is_off()) { was_off = true; return; }
    bool result = chronvs_weather_take_result(&snapshot,NULL,0);
    bool update_age = dirty || result || was_off || lv_tick_elaps(age_tick) >= 60000;
    if (dirty || result || shown_state != chronvs_weather_state()) render();
    if (update_age) {
        char text[80];
        chronvs_time_t time = {0};
        if (snapshot.valid) time = chronvs_rtc_read();
        chronvs_weather_age(&snapshot,chronvs_weather_local_epoch(&time),text,sizeof(text));
        set_text(age,text);
        age_tick = lv_tick_get();
    }
    was_off = false;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int y, int width, const lv_font_t *font) {
    lv_obj_t *obj = chronvs_aion_label(parent,text,y,font);
    lv_obj_set_width(obj,width);
    lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
    return obj;
}

static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    chronvs_aion_surface(root);
    lv_obj_t *title = label(root,"CLIMA",34,180,&lv_font_montserrat_24);
    lv_obj_set_style_text_color(title,lv_color_hex(CHRONVS_UI_ACCENT),0);
    label(root,"São Paulo",68,200,&chronvs_mnemo_font);
    symbol = lv_obj_create(root);
    lv_obj_remove_style_all(symbol);
    lv_obj_set_size(symbol,56,48);
    lv_obj_align(symbol,LV_ALIGN_TOP_MID,0,99);
    lv_obj_clear_flag(symbol,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(symbol,draw_symbol,LV_EVENT_DRAW_MAIN,NULL);
    temperature = label(root,"--°C",151,280,&lv_font_montserrat_48);
    condition = label(root,"Sem dados",210,300,&chronvs_mnemo_font);
    apparent = label(root,"",241,280,&chronvs_mnemo_font);
    humidity = label(root,"",266,280,&chronvs_mnemo_font);
    range = label(root,"",291,280,&chronvs_mnemo_font);
    age = label(root,"Sem dados",324,270,&chronvs_mnemo_font);
    status = label(root,"",349,240,&chronvs_mnemo_font);
    label(root,"Open-Meteo",380,140,&lv_font_montserrat_12);
    chronvs_ui_app_input_bind(root,&input);
    refresh = lv_timer_create(poll,250,NULL);
    lv_timer_pause(refresh);
    return root;
}

static void show(void) {
    visible = true;
    dirty = true;
    input.consumed = false;
    chronvs_weather_init();
    chronvs_weather_get_snapshot(&snapshot);
    /* Reopening during an existing request joins it; never queues a second one. */
    chronvs_weather_request_update();
    lv_timer_resume(refresh);
    poll(refresh);
}

static void hide(void) {
    visible = false;
    lv_timer_pause(refresh);
}

const chronvs_app_t chronvs_weather_app = {
    .id = "weather", .name = "Clima", .launcher_visible = true,
    .create_icon = create_icon, .create = create, .on_show = show, .on_hide = hide,
};
CHRONVS_REGISTER_APP(chronvs_weather_app)
