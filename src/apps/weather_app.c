#include "apps/app_catalog.h"

#include <stdio.h>
#include <string.h>
#include "core/app_manager.h"
#include "services/weather_data.h"
#include "services/weather_service.h"
#include "ui/Relogio_widgets.h"
#include "ui/app_input.h"
#include "ui/Notas_font.h"
#include "ui/system_ui.h"
#include "ui/weather_art.h"
#include "ui/weather_face.h"

static lv_obj_t *temperature, *temperature_shadow, *condition;
static lv_obj_t *apparent, *humidity, *minimum, *maximum;
static lv_obj_t *age, *age_marker, *status, *symbol;
static lv_obj_t *metric_chrome[6];
static lv_timer_t *refresh;
static chronvs_weather_snapshot_t snapshot;
static chronvs_weather_state_t shown_state;
static bool visible, dirty, was_off;
static uint32_t age_tick;
static void back(void) { chronvs_app_open("apps"); }
static chronvs_ui_app_input_t input = {.back = back};

static void create_icon(lv_obj_t *parent) {
    lv_obj_t *image = lv_img_create(parent);
    lv_img_set_src(image, chronvs_weather_art_44(WEATHER_PARTLY_CLOUDY));
    lv_obj_center(image);
}

static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void render(void) {
    char text[80];
    if (snapshot.valid) {
        snprintf(text,sizeof(text),"%.0f°C",snapshot.temperature_c);
        set_text(temperature,text);
        set_text(temperature_shadow,text);
        set_text(condition,chronvs_weather_condition(snapshot.weather_code));
        snprintf(text,sizeof(text),"%.0f°C",snapshot.apparent_temperature_c);
        set_text(apparent,text);
        snprintf(text,sizeof(text),"%u%%",snapshot.humidity_percent);
        set_text(humidity,text);
        snprintf(text,sizeof(text),"%.0f°",snapshot.minimum_c);
        set_text(minimum,text);
        snprintf(text,sizeof(text),"%.0f°",snapshot.maximum_c);
        set_text(maximum,text);
    } else {
        set_text(temperature,"--°C");
        set_text(temperature_shadow,"--°C");
        set_text(condition,"Sem dados");
        set_text(apparent,""); set_text(humidity,"");
        set_text(minimum,""); set_text(maximum,"");
    }
    shown_state = chronvs_weather_state();
    set_text(status,shown_state == CHRONVS_WEATHER_FETCHING ? "Atualizando..." :
        shown_state == CHRONVS_WEATHER_ERROR || *chronvs_weather_error() ?
            (snapshot.valid ? "Falha ao atualizar" : chronvs_weather_error()) : "");
    if (snapshot.valid) {
        lv_img_set_src(symbol, chronvs_weather_art_88(
            chronvs_weather_icon(snapshot.weather_code)));
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(age, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(age_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(age, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(age_marker, LV_OBJ_FLAG_HIDDEN);
    }
    for (unsigned i = 0; i < 6; ++i) {
        if (snapshot.valid) lv_obj_clear_flag(metric_chrome[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(metric_chrome[i], LV_OBJ_FLAG_HIDDEN);
    }
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
        int64_t now = chronvs_weather_local_epoch(&time);
        uint32_t color = CHRONVS_UI_TEXT_DIM;
        if (snapshot.valid && now >= snapshot.updated_epoch) {
            int64_t minutes = (now - snapshot.updated_epoch) / 60;
            if (minutes < 1) {
                snprintf(text,sizeof(text),"Agora");
                color = 0xA5D9A4;
            } else if (minutes < 60) {
                snprintf(text,sizeof(text),"Há %lld min",(long long)minutes);
                color = 0xD8BE75;
            } else if (minutes < 1440) {
                snprintf(text,sizeof(text),"Há %lld h",(long long)(minutes / 60));
                color = 0xD99678;
            } else {
                snprintf(text,sizeof(text),"Há %lld d",(long long)(minutes / 1440));
                color = 0xD99678;
            }
        } else {
            snprintf(text,sizeof(text),"Sem horário");
        }
        set_text(age,text);
        lv_obj_set_style_bg_color(age_marker,lv_color_hex(color),0);
        lv_obj_set_style_text_color(age,lv_color_hex(color),0);
        age_tick = lv_tick_get();
    }
    was_off = false;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, const lv_font_t *font) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj,text);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_width(obj,width);
    lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_LEFT,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
    return obj;
}

static void draw_feels_icon(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event),&area);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_draw_rect_dsc_t metal;
    lv_draw_rect_dsc_init(&metal);
    metal.bg_color = lv_color_hex(CHRONVS_UI_TEXT_DIM);
    metal.radius = LV_RADIUS_CIRCLE;
    lv_area_t stem = {area.x1 + 9,area.y1 + 2,area.x1 + 13,area.y1 + 19};
    lv_draw_rect(ctx,&metal,&stem);
    lv_area_t bulb = {area.x1 + 6,area.y1 + 16,area.x1 + 16,area.y1 + 26};
    lv_draw_rect(ctx,&metal,&bulb);
    metal.bg_color = lv_color_hex(CHRONVS_UI_ACCENT);
    lv_area_t core = {area.x1 + 9,area.y1 + 19,area.x1 + 13,area.y1 + 23};
    lv_draw_rect(ctx,&metal,&core);
}

static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    chronvs_Relogio_surface(root);
    for (unsigned i = 0; i < 5; ++i) {
        if (i == 2) continue; /* Divider removed from the visible face. */
        lv_obj_t *layer = lv_img_create(root);
        lv_img_set_src(layer,chronvs_weather_face_layers[i].image);
        lv_obj_set_pos(layer,chronvs_weather_face_layers[i].x + (i == 1 ? 13 : 0),
                       chronvs_weather_face_layers[i].y + (i == 1 ? 75 : 0));
        if (i == 0) {
            lv_img_set_pivot(layer,206,206);
            lv_img_set_zoom(layer,270);
        }
        lv_obj_clear_flag(layer,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t *city = label(root,"São Paulo",80,73,252,&chronvs_Notas_font);
    lv_obj_set_style_text_align(city,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(city,lv_color_hex(CHRONVS_UI_ACCENT),0);
    age_marker = lv_obj_create(root);
    lv_obj_set_size(age_marker,10,10);
    lv_obj_set_pos(age_marker,81,110);
    lv_obj_set_style_radius(age_marker,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_width(age_marker,0,0);
    lv_obj_set_style_pad_all(age_marker,0,0);
    lv_obj_set_style_bg_opa(age_marker,LV_OPA_COVER,0);
    lv_obj_clear_flag(age_marker,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    age = label(root,"Sem horário",100,103,180,&chronvs_Notas_font);
    lv_obj_set_style_text_color(age,lv_color_hex(CHRONVS_UI_TEXT_DIM),0);
    status = label(root,"",80,130,190,&chronvs_Notas_font);
    lv_obj_set_style_text_color(status,lv_color_hex(CHRONVS_UI_TEXT_DIM),0);
    symbol = lv_img_create(root);
    lv_img_set_src(symbol,chronvs_weather_art_88(WEATHER_PARTLY_CLOUDY));
    lv_obj_set_pos(symbol,264,144);
    lv_obj_add_flag(symbol,LV_OBJ_FLAG_HIDDEN);
    temperature_shadow = label(root,"--°C",81,157,264,&lv_font_montserrat_48);
    lv_obj_set_style_text_color(temperature_shadow,lv_color_hex(0x111C15),0);
    temperature = label(root,"--°C",80,155,264,&lv_font_montserrat_48);
    condition = label(root,"Sem dados",80,215,276,&chronvs_Notas_font);
    metric_chrome[0] = label(root,"AGORA",105,265,78,&lv_font_montserrat_12);
    metric_chrome[1] = label(root,"HOJE",259,265,66,&lv_font_montserrat_12);
    lv_obj_t *feels_icon = lv_obj_create(root);
    lv_obj_remove_style_all(feels_icon);
    lv_obj_set_size(feels_icon,22,28);
    lv_obj_set_pos(feels_icon,94,289);
    lv_obj_add_event_cb(feels_icon,draw_feels_icon,LV_EVENT_DRAW_MAIN,NULL);
    lv_obj_clear_flag(feels_icon,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    metric_chrome[2] = feels_icon;
    metric_chrome[3] = label(root,LV_SYMBOL_TINT,96,331,24,&lv_font_montserrat_24);
    metric_chrome[4] = label(root,LV_SYMBOL_UP,230,289,24,&lv_font_montserrat_24);
    metric_chrome[5] = label(root,LV_SYMBOL_DOWN,230,331,24,&lv_font_montserrat_24);
    for (unsigned i = 0; i < 6; ++i)
        lv_obj_set_style_text_color(metric_chrome[i],lv_color_hex(CHRONVS_UI_TEXT_DIM),0);
    apparent = label(root,"",104,288,90,&lv_font_montserrat_24);
    humidity = label(root,"",104,330,90,&lv_font_montserrat_24);
    lv_obj_set_style_text_align(apparent,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_set_style_text_align(humidity,LV_TEXT_ALIGN_RIGHT,0);
    maximum = label(root,"",258,288,80,&lv_font_montserrat_24);
    minimum = label(root,"",258,330,80,&lv_font_montserrat_24);
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
