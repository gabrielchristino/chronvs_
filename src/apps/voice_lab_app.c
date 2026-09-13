#include "apps/app_catalog.h"

#include <stdio.h>
#include <string.h>

#include "core/app_manager.h"
#include "services/voice_lab_service.h"
#include "ui/aion_widgets.h"
#include "ui/app_input.h"
#include "ui/control_style.h"
#include "ui/mnemo_font.h"

static lv_obj_t *status_label, *text_label, *mic_button, *mic_caption;
static lv_timer_t *refresh;
static bool visible;
static char phrase[280];
static chronvs_voice_state_t shown_state;
static chronvs_ui_app_input_t input;

static void stop(void) { chronvs_voice_lab_stop(); }
static void back(void) { stop(); chronvs_app_open("apps"); }

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text, int y,
                                int width, const lv_font_t *font) {
    lv_obj_t *label = chronvs_aion_label(parent, text, y, font);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static void set_state(chronvs_voice_state_t state) {
    shown_state = state;
    const char *caption = "Tocar para ouvir";
    if (state == CHRONVS_VOICE_STARTING) caption = "Carregando modelo...";
    else if (state == CHRONVS_VOICE_LISTENING) caption = "Ouvindo... fale cadenciado";
    else if (state == CHRONVS_VOICE_STOPPING) caption = "Encerrando...";
    else if (state == CHRONVS_VOICE_ERROR) caption = chronvs_voice_lab_error();
    lv_label_set_text(status_label, caption);
    lv_label_set_text(mic_caption,
        state == CHRONVS_VOICE_LISTENING || state == CHRONVS_VOICE_STARTING ?
        LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
    if (state == CHRONVS_VOICE_LISTENING) lv_obj_add_state(mic_button, LV_STATE_CHECKED);
    else lv_obj_clear_state(mic_button, LV_STATE_CHECKED);
}

static void mic_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED || input.consumed) return;
    chronvs_voice_state_t state = chronvs_voice_lab_state();
    if (state == CHRONVS_VOICE_STARTING || state == CHRONVS_VOICE_LISTENING)
        chronvs_voice_lab_stop();
    else chronvs_voice_lab_start();
    set_state(chronvs_voice_lab_state());
}

static void clear_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED || input.consumed) return;
    phrase[0] = '\0';
    lv_label_set_text(text_label, "As palavras reconhecidas aparecem aqui.");
}

static void append_result(const chronvs_voice_result_t *result) {
    const size_t used = strlen(phrase);
    const size_t needed = strlen(result->text) + (used ? 1 : 0);
    if (needed >= sizeof(phrase) - used) return;
    if (used) strcat(phrase, " ");
    strcat(phrase, result->text);
    lv_label_set_text(text_label, phrase);
}

static void poll(lv_timer_t *timer) {
    (void)timer;
    if (!visible) return;
    chronvs_voice_result_t result;
    while (chronvs_voice_lab_take_result(&result)) append_result(&result);
    chronvs_voice_state_t state = chronvs_voice_lab_state();
    if (state != shown_state) set_state(state);
}

static void draw_icon(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target(event);
    lv_area_t area; lv_obj_get_coords(object, &area);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_draw_arc_dsc_t arc; lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_hex(CHRONVS_UI_ACCENT); arc.width = 3;
    lv_point_t center = {(area.x1 + area.x2) / 2, area.y1 + 19};
    lv_draw_arc(ctx, &arc, &center, 8, 0, 360);
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(CHRONVS_UI_TEXT); line.width = 2;
    lv_point_t stem[] = {{center.x, center.y + 8}, {center.x, center.y + 15}};
    lv_point_t base[] = {{center.x - 6, center.y + 15}, {center.x + 6, center.y + 15}};
    lv_draw_line(ctx, &line, &stem[0], &stem[1]);
    lv_draw_line(ctx, &line, &base[0], &base[1]);
}

static void create_icon(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, draw_icon, LV_EVENT_DRAW_MAIN, NULL);
}

static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    chronvs_aion_surface(root);
    lv_obj_t *title = centered_label(root, "VOX", 34, 160, &lv_font_montserrat_24);
    lv_obj_set_style_text_color(title, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    centered_label(root, "Laboratório offline", 70, 230, &chronvs_mnemo_font);

    text_label = centered_label(root, "As palavras reconhecidas aparecem aqui.",
                                112, 280, &chronvs_mnemo_font);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(text_label, 120);

    status_label = centered_label(root, "Tocar para ouvir", 246, 280,
                                  &lv_font_montserrat_12);
    mic_button = lv_btn_create(root);
    chronvs_ui_style_control(mic_button, false);
    lv_obj_set_size(mic_button, 70, 70);
    lv_obj_align(mic_button, LV_ALIGN_TOP_MID, 0, 276);
    lv_obj_set_style_bg_color(mic_button, lv_color_hex(CHRONVS_UI_ACCENT), LV_STATE_CHECKED);
    lv_obj_add_event_cb(mic_button, mic_event, LV_EVENT_ALL, NULL);
    mic_caption = lv_label_create(mic_button);
    lv_obj_set_style_text_font(mic_caption, &lv_font_montserrat_24, 0);
    lv_label_set_text(mic_caption, LV_SYMBOL_AUDIO);
    lv_obj_center(mic_caption);

    lv_obj_t *clear = chronvs_aion_action(root, "Limpar", 0, 354,
        CHRONVS_UI_ACTION_WIDTH, true, clear_event, 0);
    (void)clear;
    input.back = back;
    chronvs_ui_app_input_bind(root, &input);
    refresh = lv_timer_create(poll, 50, NULL);
    lv_timer_pause(refresh);
    return root;
}

static void show(void) {
    visible = true;
    input.consumed = false;
    set_state(chronvs_voice_lab_state());
    lv_timer_resume(refresh);
}

static void hide(void) {
    visible = false;
    stop();
    lv_timer_pause(refresh);
}

const chronvs_app_t chronvs_voice_lab_app = {
    .id = "vox", .name = "Vox", .launcher_visible = true,
    .create_icon = create_icon, .create = create, .on_show = show, .on_hide = hide,
};
CHRONVS_REGISTER_APP(chronvs_voice_lab_app)
