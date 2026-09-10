#include "ui/weather_icon.h"
#include "ui/control_style.h"

typedef struct { lv_draw_ctx_t *ctx; int x, y, size; } canvas_t;

static lv_point_t point(const canvas_t *c, int x, int y) {
    return (lv_point_t){c->x + x * c->size / 44, c->y + y * c->size / 44};
}

static void rect(const canvas_t *c, lv_draw_rect_dsc_t *dsc,
                 int x, int y, int x2, int y2) {
    lv_point_t a = point(c,x,y), b = point(c,x2,y2);
    lv_area_t area = {a.x,a.y,b.x,b.y};
    lv_draw_rect(c->ctx,dsc,&area);
}

static void line(const canvas_t *c, int x, int y, int x2, int y2, lv_color_t color) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = 2;
    dsc.round_start = dsc.round_end = true;
    lv_point_t a = point(c,x,y), b = point(c,x2,y2);
    lv_draw_line(c->ctx,&dsc,&a,&b);
}

void chronvs_ui_draw_weather_icon(lv_draw_ctx_t *ctx, const lv_area_t *bounds,
                                  chronvs_weather_icon_t icon) {
    int width = lv_area_get_width(bounds), height = lv_area_get_height(bounds);
    int size = LV_MIN(44,LV_MIN(width,height));
    if (size <= 0) return;
    canvas_t c = {ctx,bounds->x1+(width-size)/2,bounds->y1+(height-size)/2,size};
    lv_color_t accent = lv_color_hex(CHRONVS_UI_ACCENT), text = lv_color_hex(CHRONVS_UI_TEXT);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_width = 2;
    dsc.border_color = accent;
    if (icon == WEATHER_SUN || icon == WEATHER_PARTLY_CLOUDY) {
        int sx = icon == WEATHER_SUN ? 22 : 15;
        int sy = icon == WEATHER_SUN ? 22 : 15;
        rect(&c,&dsc,sx-7,sy-7,sx+7,sy+7);
        static const int8_t rays[8][4] = {
            {-11,0,-15,0},{11,0,15,0},{0,-11,0,-15},{0,11,0,15},
            {-8,-8,-11,-11},{8,-8,11,-11},{-8,8,-11,11},{8,8,11,11},
        };
        for (unsigned i=0; i<8; ++i)
            line(&c,sx+rays[i][0],sy+rays[i][1],sx+rays[i][2],sy+rays[i][3],accent);
    }
    if (icon != WEATHER_SUN) {
        dsc.border_color = text;
        dsc.bg_color = lv_color_hex(CHRONVS_UI_PANEL);
        dsc.bg_opa = LV_OPA_COVER;
        rect(&c,&dsc,7,20,39,31);
        rect(&c,&dsc,16,12,33,29);
        dsc.border_width = 0;
        dsc.radius = 0;
        rect(&c,&dsc,12,23,34,29);
        if (icon == WEATHER_FOG) {
            line(&c,8,36,36,36,text);
            line(&c,12,41,32,41,text);
        } else if (icon == WEATHER_STORM) {
            line(&c,25,32,20,37,accent);
            line(&c,20,37,25,37,accent);
            line(&c,25,37,21,43,accent);
        } else if (icon == WEATHER_RAIN || icon == WEATHER_SNOW) {
            for (int dx=12; dx<=32; dx+=10) {
                line(&c,dx,35,dx-3,41,accent);
                if (icon == WEATHER_SNOW) line(&c,dx-4,36,dx+1,40,text);
            }
        }
    }
}
