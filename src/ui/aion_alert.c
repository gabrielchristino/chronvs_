#include "ui/aion_alert.h"
#include "ui/aion_widgets.h"
#include "ui/system_ui.h"
#include "services/aion_service.h"
#include "services/sound_service.h"
#include "ui/mnemo_font.h"

static lv_obj_t *overlay;
static int shown = -2;
/* Hardware isolation: let the alert render before I2S is initialized/enabled. */
#define ALERT_SOUND_DELAY_MS 2000
static bool sound_pending;
static uint32_t alert_shown_tick;
static lv_obj_t *error_text;

static void dismiss(lv_event_t *event) {
    if (shown >= CHRONVS_ALARM_LIMIT &&
        !chronvs_reminder_complete(shown - CHRONVS_ALARM_LIMIT)) {
        lv_label_set_text(error_text, "Falha ao salvar. Tente novamente.");
        return;
    }
    if (shown < CHRONVS_ALARM_LIMIT)
        chronvs_aion_dismiss((uintptr_t)lv_event_get_user_data(event));
    sound_pending = false;
    chronvs_sound_set_ringing(false);
    /* Defer tree changes until after input dispatch. */
    shown = -3;
}
void chronvs_aion_alert_poll(void) {
    int alert = chronvs_aion_alert();
    if (alert != -2) chronvs_system_ui_notify_activity();
    if (shown == alert) {
        if (sound_pending && lv_tick_elaps(alert_shown_tick) >= ALERT_SOUND_DELAY_MS) {
            sound_pending = false;
            chronvs_sound_set_ringing(true);
        }
        return;
    }
    if (overlay) { lv_obj_del(overlay); overlay = NULL; }
    shown = alert;
    sound_pending = false;
    chronvs_sound_set_ringing(false);
    if (alert == -2) return;
    lv_indev_t *input = lv_indev_get_next(NULL);
    while (input) { lv_indev_wait_release(input); input = lv_indev_get_next(input); }
    overlay = lv_obj_create(lv_layer_top());
    chronvs_aion_surface(overlay);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *title = chronvs_aion_label(overlay, alert == -1 ? "Timer acabou" :
        alert >= CHRONVS_ALARM_LIMIT ? "Lembrete" : "Alarme", 34, &lv_font_montserrat_24);
    lv_obj_set_style_text_color(title, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    if (alert == -1) {
        const int minutes[] = {1,5,10,15,30,60,120};
        const char *texts[] = {"+1", "+5", "+10", "+15", "+30", "+60", "+120"};
        for (int i = 0; i < 7; ++i)
            chronvs_aion_circle(overlay, texts[i], i, 88, dismiss, minutes[i]);
        chronvs_aion_action(overlay, "Parar", 0, 314, CHRONVS_UI_ACTION_WIDTH, false, dismiss, 0);
    } else if (alert >= CHRONVS_ALARM_LIMIT) {
        const chronvs_reminder_t *r = chronvs_reminder_get(alert - CHRONVS_ALARM_LIMIT);
        lv_obj_t *text = chronvs_aion_label(overlay, r ? r->title : "", 112, &chronvs_mnemo_font);
        lv_obj_set_width(text, 280);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *time = chronvs_aion_label(overlay, "", 220, &lv_font_montserrat_18);
        if (r) lv_label_set_text_fmt(time, "%02u/%02u/%04u  %02u:%02u",
            r->day, r->month, 2000u+r->year, r->hour, r->minute);
        chronvs_aion_action(overlay, "Concluir", 0, 280, CHRONVS_UI_ACTION_WIDTH, false, dismiss, 0);
        error_text = chronvs_aion_label(overlay, "", 346, &lv_font_montserrat_12);
    } else {
        const chronvs_alarm_t *alarm = chronvs_alarm_get(alert);
        lv_obj_t *time = chronvs_aion_label(overlay, "", 126, &lv_font_montserrat_48);
        if (alarm) lv_label_set_text_fmt(time, "%02u:%02u", alarm->hour, alarm->minute);
        chronvs_aion_action(overlay, "+5 min", -66, 264, CHRONVS_UI_PAIR_WIDTH, true, dismiss, 5);
        chronvs_aion_action(overlay, "Parar", 66, 264, CHRONVS_UI_PAIR_WIDTH, false, dismiss, 0);
    }
    alert_shown_tick = lv_tick_get();
    sound_pending = true;
}
