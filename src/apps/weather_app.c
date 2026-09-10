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

static lv_obj_t *temperature, *condition, *apparent, *humidity, *range, *age, *status, *symbol;
static lv_timer_t *refresh;
static chronvs_weather_snapshot_t snapshot;
static chronvs_weather_state_t shown_state;
static bool visible, dirty, was_off;
static uint32_t age_tick;
static void back(void) { chronvs_app_open("apps"); }
static chronvs_ui_app_input_t input = {.back = back};

static void line(lv_draw_ctx_t *ctx, int x, int y, int x2, int y2, lv_color_t color) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = 2;
    dsc.round_start = dsc.round_end = true;
    lv_point_t a = {x,y}, b = {x2,y2};
    lv_draw_line(ctx, &dsc, &a, &b);
}

static void draw_symbol(lv_event_t *event) {
    const bool launcher = lv_event_get_user_data(event) != NULL;
    if (!launcher && !snapshot.valid) return;
    chronvs_weather_icon_t icon = launcher ? WEATHER_PARTLY_CLOUDY : chronvs_weather_icon(snapshot.weather_code);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    int x = bounds.x1 + (lv_area_get_width(&bounds) - 44) / 2;
    int y = bounds.y1 + (lv_area_get_height(&bounds) - 44) / 2;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_color_t accent = lv_color_hex(CHRONVS_UI_ACCENT), text = lv_color_hex(CHRONVS_UI_TEXT);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_width = 2;
    dsc.border_color = accent;
    if (icon == WEATHER_SUN || icon == WEATHER_PARTLY_CLOUDY) {
        int sx = icon == WEATHER_SUN ? x + 22 : x + 15;
        int sy = icon == WEATHER_SUN ? y + 22 : y + 15;
        lv_area_t sun = {sx-7, sy-7, sx+7, sy+7};
        lv_draw_rect(ctx, &dsc, &sun);
        static const int8_t rays[8][4] = {
            {-11,0,-15,0},{11,0,15,0},{0,-11,0,-15},{0,11,0,15},
            {-8,-8,-11,-11},{8,-8,11,-11},{-8,8,-11,11},{8,8,11,11},
        };
        for (unsigned i=0; i<8; ++i)
            line(ctx,sx+rays[i][0],sy+rays[i][1],sx+rays[i][2],sy+rays[i][3],accent);
    }
    if (icon != WEATHER_SUN) {
        dsc.border_color = text;
        dsc.bg_color = lv_color_hex(CHRONVS_UI_PANEL);
        dsc.bg_opa = LV_OPA_COVER;
        lv_area_t cloud = {x+7,y+20,x+39,y+31};
        lv_draw_rect(ctx,&dsc,&cloud);
        cloud = (lv_area_t){x+16,y+12,x+33,y+29};
        lv_draw_rect(ctx,&dsc,&cloud);
        dsc.border_width = 0;
        dsc.radius = 0;
        cloud = (lv_area_t){x+12,y+23,x+34,y+29};
        lv_draw_rect(ctx,&dsc,&cloud);
        if (icon == WEATHER_FOG) {
            line(ctx,x+8,y+36,x+36,y+36,text);
            line(ctx,x+12,y+41,x+32,y+41,text);
        } else if (icon == WEATHER_STORM) {
            line(ctx,x+25,y+32,x+20,y+37,accent);
            line(ctx,x+20,y+37,x+25,y+37,accent);
            line(ctx,x+25,y+37,x+21,y+43,accent);
        } else if (icon == WEATHER_RAIN || icon == WEATHER_SNOW) {
            for (int dx=12; dx<=32; dx+=10) {
                line(ctx,x+dx,y+35,x+dx-3,y+41,accent);
                if (icon == WEATHER_SNOW) line(ctx,x+dx-4,y+36,x+dx+1,y+40,text);
            }
        }
    }
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
