#include "apps/app_catalog.h"
#include "services/run_service.h"
#include "services/agnss_service.h"
#include "ui/Relogio_widgets.h"
#include "ui/Notas_font.h"
#include "ui/app_input.h"
#include "ui/system_ui.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *main_page, *history_page, *confirm_page;
static lv_obj_t *status, *elapsed, *distance, *speed, *pace, *detail, *primary, *finish;
static lv_obj_t *files;
static lv_timer_t *timer;
enum { MAIN, HISTORY, CONFIRM };
static unsigned page;
static void refresh(lv_timer_t *unused);
static void show_page(unsigned next) {
    page = next;
    lv_obj_add_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(history_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(confirm_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(next == HISTORY ? history_page : next == CONFIRM ? confirm_page : main_page, LV_OBJ_FLAG_HIDDEN);
    if (next == HISTORY) {
        char names[5][48], text[300] = "";
        unsigned count = chronvs_run_files(names, 5);
        for (unsigned i = 0; i < count; ++i) {
            strncat(text, names[i], sizeof(text)-strlen(text)-1);
            strncat(text, "\n\n", sizeof(text)-strlen(text)-1);
        }
        lv_label_set_text(files, count ? text : "Nenhum registro ou SD indisponível");
    }
}
static void back(void) {
    if (page != MAIN) show_page(MAIN);
    else chronvs_app_open("apps");
}
static void vertical(int direction) {
    if (page == MAIN && direction > 0) show_page(HISTORY);
    else if (page == HISTORY && direction < 0) show_page(MAIN);
}
static chronvs_ui_app_input_t input = {.back = back, .vertical = vertical};
static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static void action(lv_event_t *event) {
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    intptr_t choice = (intptr_t)lv_event_get_user_data(event);
    chronvs_run_snapshot_t s; chronvs_run_snapshot(&s);
    if (choice == 2) {
        chronvs_run_finish(); show_page(MAIN);
    } else if (choice == 1) show_page(CONFIRM);
    else if (s.state == RUN_RECORDING) chronvs_run_pause();
    else if (s.state == RUN_PAUSED) chronvs_run_resume();
    else if (s.state == RUN_READY) chronvs_run_start();
    else chronvs_run_prepare();
    refresh(NULL);
}
static void refresh(lv_timer_t *unused) {
    (void)unused;
    if (chronvs_system_ui_display_is_off()) return;
    chronvs_run_snapshot_t s; chronvs_run_snapshot(&s);
    set_text(status, s.message);
    bool summary = s.state == RUN_SAVED && s.active_s && s.distance_m > 0;
    if (summary) { s.speed_kmh = s.distance_m * 3.6 / s.active_s; s.speed_valid = true; }
    char text[96];
    snprintf(text, sizeof(text), "%02lu:%02lu:%02lu", (unsigned long)(s.active_s/3600),
        (unsigned long)(s.active_s/60%60), (unsigned long)(s.active_s%60));
    set_text(elapsed, text);
    snprintf(text, sizeof(text), "%.2f km", s.distance_m/1000); set_text(distance, text);
    if (s.speed_valid) snprintf(text, sizeof(text), "%s%.1f km/h", summary ? "Média " : "", s.speed_kmh);
    else snprintf(text, sizeof(text), "-- km/h");
    set_text(speed, text);
    if (s.speed_valid && s.speed_kmh >= 1) {
        unsigned seconds = (unsigned)(3600/s.speed_kmh + 0.5);
        snprintf(text, sizeof(text), "%u:%02u min/km", seconds/60, seconds%60);
    } else snprintf(text, sizeof(text), "--:-- min/km");
    set_text(pace, text);
    if (s.state == RUN_SAVED || s.state == RUN_ERROR) snprintf(text, sizeof(text), "%s", s.file);
    else if (s.state==RUN_READY) {
        chronvs_agnss_snapshot_t assist; chronvs_agnss_snapshot(&assist);
        const char *phase=assist.state==AGNSS_WAIT_TIME ? "aguardando NTP" :
            assist.state==AGNSS_DOWNLOAD ? "baixando Wi-Fi" : assist.state==AGNSS_READY ? "preparada" :
            assist.state==AGNSS_SENDING ? "enviando" : assist.state==AGNSS_SENT ? "enviada" :
            assist.state==AGNSS_ERROR ? "falha; consulte GPS" : "GPS independente";
        snprintf(text,sizeof(text),"A-GNSS: %s",phase);
    } else snprintf(text, sizeof(text), "Total %lu:%02lu  /  %lu pontos", (unsigned long)(s.elapsed_s/60),
        (unsigned long)(s.elapsed_s%60), (unsigned long)s.points);
    set_text(detail, text);
    const char *caption = s.state == RUN_RECORDING ? "Pausar" : s.state == RUN_PAUSED ? "Retomar" :
        s.state == RUN_READY ? "Iniciar" : s.state == RUN_ERROR ? "Tentar" : "Nova";
    set_text(lv_obj_get_child(primary, 0), caption);
    bool ongoing = s.state == RUN_RECORDING || s.state == RUN_PAUSED;
    if (ongoing) lv_obj_clear_flag(finish, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(finish, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(primary, LV_ALIGN_TOP_MID, ongoing ? -66 : 0, 308);
    lv_obj_set_width(primary, ongoing ? 120 : 140);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int y, int width, const lv_font_t *font) {
    lv_obj_t *obj = chronvs_Relogio_label(parent, text, y, font);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    return obj;
}
static void icon(lv_obj_t *parent) {
    /* Running figure, distinct from location arrow and clock faces. */
    lv_obj_t *head = lv_obj_create(parent);
    lv_obj_remove_style_all(head); lv_obj_set_size(head, 8, 8); lv_obj_set_pos(head, 25, 3);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    static const lv_point_t body[] = {{7,18},{16,12},{25,16},{34,22},{40,16}};
    static const lv_point_t legs[] = {{25,16},{20,26},{9,26},{4,35}};
    static const lv_point_t foot[] = {{20,26},{30,32},{25,41}};
    const lv_point_t *points[] = {body, legs, foot};
    unsigned counts[] = {5,4,3};
    for (unsigned i=0; i<3; ++i) {
        lv_obj_t *line = lv_line_create(parent); lv_line_set_points(line, points[i], counts[i]);
        lv_obj_set_style_line_color(line, lv_color_hex(CHRONVS_UI_ACCENT), 0);
        lv_obj_set_style_line_width(line, 3, 0); lv_obj_set_style_line_rounded(line, true, 0);
    }
}
static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent); chronvs_Relogio_surface(root);
    main_page = lv_obj_create(root); chronvs_Relogio_surface(main_page);
    history_page = lv_obj_create(root); chronvs_Relogio_surface(history_page);
    confirm_page = lv_obj_create(root); chronvs_Relogio_surface(confirm_page);
    status = label(main_page, "", 58, 290, &chronvs_Notas_font);
    lv_obj_set_style_text_color(status, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    elapsed = label(main_page, "00:00:00", 105, 330, &lv_font_montserrat_48);
    distance = label(main_page, "0.00 km", 172, 300, &lv_font_montserrat_24);
    speed = label(main_page, "-- km/h", 211, 280, &chronvs_Notas_font);
    pace = label(main_page, "--:-- min/km", 242, 280, &chronvs_Notas_font);
    detail = label(main_page, "", 278, 290, &lv_font_montserrat_12);
    primary = chronvs_Relogio_action(main_page, "Iniciar", 0, 308, 120, false, action, 0);
    finish = chronvs_Relogio_action(main_page, "Finalizar", 66, 308, 120, true, action, 1);
    label(history_page, "Registros no SD", 60, 270, &chronvs_Notas_font);
    files = label(history_page, "", 112, 300, &lv_font_montserrat_12);
    lv_obj_set_height(files, 180);
    label(history_page, "chronvs/runs\nGPX para importar / CSV original\nDesligue antes de retirar o SD", 304, 250, &lv_font_montserrat_12);
    label(confirm_page, "Finalizar corrida?", 117, 280, &lv_font_montserrat_24);
    label(confirm_page, "Salvar percurso e resumo no SD\nDireita cancela", 189, 280, &chronvs_Notas_font);
    chronvs_Relogio_action(confirm_page, "Salvar", 0, 280, 140, false, action, 2);
    chronvs_ui_app_input_bind(root, &input);
    timer = lv_timer_create(refresh, 1000, NULL); lv_timer_pause(timer);
    show_page(MAIN);
    return root;
}
static void show(void) {
    input.consumed = false; show_page(MAIN);
    chronvs_run_snapshot_t s; chronvs_run_snapshot(&s);
    if (s.state == RUN_IDLE) chronvs_run_prepare();
    lv_timer_resume(timer); refresh(NULL);
}
static void hide(void) { lv_timer_pause(timer); chronvs_run_release_preview(); }
const chronvs_app_t chronvs_run_app = {
    .id = "run", .name = "Corrida", .launcher_visible = true,
    .create_icon = icon, .create = create, .on_show = show, .on_hide = hide,
};
CHRONVS_REGISTER_APP(chronvs_run_app)
