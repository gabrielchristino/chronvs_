#include "apps/app_catalog.h"
#include <stdio.h>
#include <string.h>
#include "services/gps_service.h"
#include "services/agnss_service.h"
#include "ui/Relogio_widgets.h"
#include "ui/Notas_font.h"
#include "ui/app_input.h"
#include "ui/system_ui.h"

static lv_obj_t *status, *latitude, *longitude, *satellites, *motion, *quality, *serial, *sky;
static lv_timer_t *refresh;
static lv_obj_t *data_page, *assist_page, *assist_button;
static char assist_lines[4][100];
static char assist_source[64];
static unsigned current_page;
#ifdef CHRONVS_GPS_STANDBY_TEST
static lv_obj_t *test_page, *test_status, *reply, *remaining, *query_button, *test_button;
#endif
static void page(int direction) {
    unsigned max=1;
#ifdef CHRONVS_GPS_STANDBY_TEST
    max=2;
#endif
    if (direction>0 && current_page<max) ++current_page;
    if (direction<0 && current_page) --current_page;
    lv_obj_add_flag(data_page,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(assist_page,LV_OBJ_FLAG_HIDDEN);
#ifdef CHRONVS_GPS_STANDBY_TEST
    lv_obj_add_flag(test_page,LV_OBJ_FLAG_HIDDEN);
    if (current_page==2) lv_obj_clear_flag(test_page,LV_OBJ_FLAG_HIDDEN);
#endif
    if (current_page<2) lv_obj_clear_flag(current_page ? assist_page : data_page,LV_OBJ_FLAG_HIDDEN);
}
static void back(void) {
    if (current_page) { page(-1); return; }
    chronvs_app_open("apps");
}
static chronvs_ui_app_input_t input = {.back = back, .vertical = page};

static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void assist_text(lv_draw_ctx_t *ctx,lv_obj_t *page_obj,const char *text,int y,int width,int height,
                        const lv_font_t *font,uint32_t color) {
    lv_area_t origin;lv_obj_get_coords(page_obj,&origin);
    lv_area_t area={origin.x1+(412-width)/2,origin.y1+y,origin.x1+(412+width)/2-1,origin.y1+y+height-1};
    lv_draw_label_dsc_t dsc;lv_draw_label_dsc_init(&dsc);
    dsc.font=font;dsc.color=lv_color_hex(color);dsc.align=LV_TEXT_ALIGN_CENTER;
    lv_draw_label(ctx,&dsc,&area,text,NULL);
}
static void assist_draw(lv_event_t *event) {
    lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(event);
    lv_obj_t *obj=lv_event_get_target(event);
    assist_text(ctx,obj,*assist_source ? assist_source : "Assistência GPS",58,300,28,&chronvs_Notas_font,CHRONVS_UI_TEXT);
    assist_text(ctx,obj,assist_lines[0],102,300,48,&chronvs_Notas_font,CHRONVS_UI_ACCENT);
    assist_text(ctx,obj,chronvs_agnss_reference_name(),164,300,28,&chronvs_Notas_font,CHRONVS_UI_TEXT);
    assist_text(ctx,obj,assist_lines[1],208,300,48,&chronvs_Notas_font,CHRONVS_UI_TEXT);
    assist_text(ctx,obj,assist_lines[2],267,300,24,&chronvs_Notas_font,CHRONVS_UI_TEXT);
    assist_text(ctx,obj,assist_lines[3],293,280,15,&lv_font_montserrat_12,CHRONVS_UI_TEXT);
}
static bool assist_line(unsigned index,const char *text) {
    if (!strcmp(assist_lines[index],text)) return false;
    snprintf(assist_lines[index],sizeof(assist_lines[index]),"%s",text);return true;
}

static void poll(lv_timer_t *timer) {
    (void)timer;
    if (chronvs_system_ui_display_is_off()) return;
    chronvs_gps_snapshot_t s;
    chronvs_gps_snapshot(&s);
    set_text(status, s.error ? "Falha na UART" : !s.running ? "Preparando..." :
        s.data.fix ? "Posição encontrada" : s.serial_fresh ? "Buscando satélites" :
        s.data.sentences ? "Serial sem sinal" : "Procurando módulo");
    char text[96];
    if (s.data.fix) snprintf(text, sizeof(text), "%+.6f°", s.data.latitude);
    else snprintf(text, sizeof(text), "--.------°");
    set_text(latitude, text);
    if (s.data.fix) snprintf(text, sizeof(text), "%+.6f°", s.data.longitude);
    else snprintf(text, sizeof(text), "--.------°");
    set_text(longitude, text);
    if (s.data.satellites_valid) snprintf(text, sizeof(text), "%u satélites em uso", s.data.satellites);
    else snprintf(text, sizeof(text), "Satélites: --");
    set_text(satellites, text);
    char speed[24] = "--", altitude[24] = "--";
    if (s.data.speed_valid) snprintf(speed, sizeof(speed), "%.1f", s.data.speed_kmh);
    if (s.data.altitude_valid) snprintf(altitude, sizeof(altitude), "%.0f", s.data.altitude_m);
    snprintf(text, sizeof(text), "%s km/h   /   %s m", speed, altitude);
    set_text(motion, text);
    char view[96] = "GSV", hdop[16] = "--", signal[16] = "--";
    bool have_view = false, have_signal = false;
    unsigned strongest = 0;
    for (unsigned i = 0; i < CHRONVS_GPS_GSV_SOURCES; ++i) {
        const chronvs_gps_sky_t *source = &s.data.sky[i];
        if (!source->valid) continue;
        have_view = true;
        char item[20]; snprintf(item, sizeof(item), " %s:%u", source->talker, source->visible);
        strncat(view, item, sizeof(view)-strlen(view)-1);
        if (source->cno_valid) {
            have_signal = true;
            if (source->cno > strongest) strongest = source->cno;
        }
    }
    set_text(sky, have_view ? view : "GSV --");
    if (have_signal) snprintf(signal, sizeof(signal), "%u", strongest);
    if (s.data.hdop_valid) snprintf(hdop, sizeof(hdop), "%.1f", s.data.hdop);
    snprintf(text, sizeof(text), "HDOP %s / C/N0 %s dBHz", hdop, signal);
    set_text(quality, text);
    snprintf(text, sizeof(text), "RX44 / %u baud\nNMEA %lu / erros %lu", s.baud,
        (unsigned long)s.data.sentences, (unsigned long)s.data.errors);
    set_text(serial, text);
    chronvs_agnss_snapshot_t assist; chronvs_agnss_snapshot(&assist);
    bool changed=assist_line(0,assist.message);
    const char *source=assist.source ? assist.source : "";
    if (strcmp(assist_source,source)) { snprintf(assist_source,sizeof(assist_source),"%s",source); changed=true; }
    snprintf(text,sizeof(text),"GPS OK %u / recusados %u\nSem ACK %u / repetições %u",assist.gps_accepted,assist.gps_rejected,
        assist.gps_unconfirmed,assist.retries);
    changed|=assist_line(1,text);
    if (assist.receiver_status) snprintf(text,sizeof(text),"Órbitas no GPS: %u",assist.receiver_ephemerides);
    else if (assist.state==AGNSS_SENDING && assist.current_satellite)
        snprintf(text,sizeof(text),"GPS %02u / tentativa %u",assist.current_satellite,assist.current_attempt);
    else if (assist.gps_confirmed_total) snprintf(text,sizeof(text),"ACK GPS no pedido: %u",assist.gps_confirmed_total);
    else snprintf(text,sizeof(text),"Status GPS: sem resposta");
    changed|=assist_line(2,text);
    if (assist.minimum_internal) snprintf(text,sizeof(text),"RAM int. %lu / DMA %lu B",
        (unsigned long)assist.minimum_internal,(unsigned long)assist.minimum_dma);
    else snprintf(text,sizeof(text),"RAM/DMA: sem amostra de rede");
    changed|=assist_line(3,text);
    if (changed && current_page==1) lv_obj_invalidate(assist_page);
    if (chronvs_agnss_active() || chronvs_gps_workout_is_active()) lv_obj_add_state(assist_button,LV_STATE_DISABLED);
    else lv_obj_clear_state(assist_button,LV_STATE_DISABLED);
#ifdef CHRONVS_GPS_STANDBY_TEST
    static const char *states[] = {"Pronto para testar", "Observando resposta...", "Serial em silêncio",
        "Serial retornou", "NMEA continuou", "Sem retorno em 70 s", "Teste interrompido", "Falha ao enviar"};
    set_text(test_status, states[s.test]);
    set_text(reply, *s.reply ? s.reply : "Consulte a versão\nantes do teste.");
    if (s.test == GPS_TEST_OBSERVING || s.test == GPS_TEST_SILENT)
        snprintf(text, sizeof(text), "%u s", s.remaining_s);
    else snprintf(text, sizeof(text), "%s", s.test == GPS_TEST_IDLE ? "Standby de 60 s" : "Consumo não medido");
    set_text(remaining, text);
    bool ready = s.running && s.serial_fresh && s.test != GPS_TEST_OBSERVING && s.test != GPS_TEST_SILENT &&
        !chronvs_agnss_active() && !chronvs_gps_workout_is_active();
    if (ready) { lv_obj_clear_state(query_button, LV_STATE_DISABLED); lv_obj_clear_state(test_button, LV_STATE_DISABLED); }
    else { lv_obj_add_state(query_button, LV_STATE_DISABLED); lv_obj_add_state(test_button, LV_STATE_DISABLED); }
#endif
}

static void update_assistance(lv_event_t *event) {
    (void)event;
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    chronvs_gps_assist_request(true); poll(NULL);
}

#ifdef CHRONVS_GPS_STANDBY_TEST
static void query(lv_event_t *event) {
    (void)event;
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    chronvs_gps_query_version(); poll(NULL);
}
static void test(lv_event_t *event) {
    (void)event;
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    chronvs_gps_test_standby(); poll(NULL);
}
#endif

static lv_obj_t *label(lv_obj_t *parent, const char *text, int y, int width, const lv_font_t *font) {
    lv_obj_t *obj = chronvs_Relogio_label(parent, text, y, font);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    return obj;
}

static void create_icon(lv_obj_t *parent) {
    /* Location arrow: distinct silhouette, contained within the 44 px canvas. */
    static const lv_point_t points[] = {{7,19},{36,7},{24,36},{20,23},{7,19}};
    lv_obj_t *arrow = lv_line_create(parent);
    lv_line_set_points(arrow, points, 5);
    lv_obj_set_style_line_width(arrow, 3, 0);
    lv_obj_set_style_line_color(arrow, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    lv_obj_set_style_line_rounded(arrow, true, 0);
}

static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    chronvs_Relogio_surface(root);
    lv_obj_t *container = root;
    data_page = lv_obj_create(container); chronvs_Relogio_surface(data_page);
    root = data_page;
    status = label(root, "Preparando...", 58, 290, &chronvs_Notas_font);
    lv_obj_set_style_text_color(status, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    label(root, "LATITUDE", 102, 240, &lv_font_montserrat_12);
    latitude = label(root, "--.------°", 121, 330, &lv_font_montserrat_24);
    label(root, "LONGITUDE", 165, 240, &lv_font_montserrat_12);
    longitude = label(root, "--.------°", 184, 330, &lv_font_montserrat_24);
    satellites = label(root, "Satélites: --", 234, 300, &chronvs_Notas_font);
    motion = label(root, "-- km/h   /   -- m", 260, 300, &chronvs_Notas_font);
    sky = label(root, "GSV --", 290, 310, &lv_font_montserrat_12);
    quality = label(root, "HDOP -- / C/N0 -- dBHz", 309, 300, &lv_font_montserrat_12);
    serial = label(root, "", 335, 230, &lv_font_montserrat_12);
    root = container;
    assist_page=lv_obj_create(root); chronvs_Relogio_surface(assist_page);
    /* Draw six read-only text fields in the page itself, preserving LVGL heap
     * headroom for the reminder editor plus alert. Interactive pill stays native. */
    lv_obj_add_event_cb(assist_page,assist_draw,LV_EVENT_DRAW_MAIN,NULL);
    assist_button=chronvs_Relogio_action(assist_page,"Atualizar",0,310,140,false,update_assistance,0);
#ifdef CHRONVS_GPS_STANDBY_TEST
    test_page = lv_obj_create(root); chronvs_Relogio_surface(test_page);
    label(test_page, "Teste de standby", 54, 270, &chronvs_Notas_font);
    reply = label(test_page, "", 104, 280, &chronvs_Notas_font);
    lv_label_set_long_mode(reply, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(reply, 100);
    test_status = label(test_page, "", 222, 300, &chronvs_Notas_font);
    lv_obj_set_style_text_color(test_status, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    remaining = label(test_page, "", 262, 280, &chronvs_Notas_font);
    query_button = chronvs_Relogio_action(test_page, "Versão", -66, 314, 120, true, query, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(query_button, 0), &chronvs_Notas_font, 0);
    test_button = chronvs_Relogio_action(test_page, "Testar 60 s", 66, 314, 120, false, test, 0);
#endif
    current_page=0; page(0);
    chronvs_ui_app_input_bind(root, &input);
    refresh = lv_timer_create(poll, 1000, NULL);
    lv_timer_pause(refresh);
    return root;
}

static void show(void) {
    input.consumed = false;
    current_page=0; page(0);
    chronvs_gps_request(true);
    chronvs_gps_assist_request(false);
    chronvs_gps_poll(chronvs_system_ui_display_is_off());
    lv_timer_resume(refresh);
    poll(refresh);
}

static void hide(void) {
    lv_timer_pause(refresh);
    chronvs_gps_request(false);
    chronvs_gps_poll(true);
}

const chronvs_app_t chronvs_gps_app = {
    .id = "gps", .name = "GPS", .launcher_visible = true,
    .create_icon = create_icon, .create = create, .on_show = show, .on_hide = hide,
};
CHRONVS_REGISTER_APP(chronvs_gps_app)
