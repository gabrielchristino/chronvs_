#include "apps/aion_pages.h"
#include <stdio.h>
#include <string.h>
#include "services/aion_service.h"
#include "ui/aion_widgets.h"
#include "ui/system_ui.h"

typedef enum { LIST, HOUR, MINUTE, DAYS, DETAIL } alarm_view_t;
static lv_obj_t *surface, *timer_text, *value_text, *arc, *error_text, *create_button;
static lv_obj_t *alarm_list;
static unsigned page, selected;
static alarm_view_t view;
static uint8_t hour, minute, days;
static bool dirty;
static uint32_t previous_remaining = UINT32_MAX;
static bool previous_running;
static const char *day_names[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};

static void rebuild(void);
static void activity(void) { chronvs_system_ui_notify_activity(); }
static void timer_action(lv_event_t *e) {
    activity();
    unsigned value = (uintptr_t)lv_event_get_user_data(e);
    if (value) chronvs_timer_start(value); else chronvs_timer_cancel();
    dirty = true;
}
static void open_alarm(lv_event_t *e) {
    activity();
    int index = (intptr_t)lv_event_get_user_data(e);
    if (index < 0) { hour = minute = days = 0; view = HOUR; }
    else { selected = index; view = DETAIL; }
    dirty = true;
}
static void next_step(lv_event_t *e) {
    (void)e;
    activity();
    if (view == HOUR) { hour = lv_arc_get_value(arc); view = MINUTE; }
    else if (view == MINUTE) { minute = lv_arc_get_value(arc); view = DAYS; }
    else if (view == DAYS) {
        if (!chronvs_alarm_create(hour, minute, days)) {
            lv_label_set_text(error_text, "Falha ao salvar");
            return;
        }
        view = LIST;
    }
    dirty = true;
}
static void delete_alarm(lv_event_t *e) {
    (void)e;
    activity();
    if (!chronvs_alarm_delete(selected)) {
        lv_label_set_text(error_text, "Falha ao excluir");
        return;
    }
    view = LIST;
    dirty = true;
}
static void choose_day(lv_event_t *e) {
    activity();
    unsigned bit = 1u << (uintptr_t)lv_event_get_user_data(e);
    days ^= bit;
    lv_obj_t *button = lv_event_get_target(e);
    if (days & bit) lv_obj_add_state(button, LV_STATE_CHECKED);
    else lv_obj_clear_state(button, LV_STATE_CHECKED);
    if (days) lv_obj_clear_state(create_button, LV_STATE_DISABLED);
    else lv_obj_add_state(create_button, LV_STATE_DISABLED);
}
static void noop(lv_event_t *e) { (void)e; }
static void back_button(lv_event_t *e) {
    (void)e;
    activity();
    chronvs_aion_pages_back();
}
static void arc_changed(lv_event_t *e) {
    (void)e;
    activity();
    /* Text is coalesced by the app's 20 ms UI timer. */
}
static void title(const char *text) {
    lv_obj_t *label = chronvs_aion_label(surface, text, 34, &lv_font_montserrat_24);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2B84B), 0);
}
static void make_days(bool editable) {
    for (unsigned i = 0; i < 7; ++i) {
        lv_obj_t *button = chronvs_aion_circle(surface, day_names[i], i, 94,
                                              editable ? choose_day : noop, i);
        if (days & (1 << i)) lv_obj_add_state(button, LV_STATE_CHECKED);
        if (!editable) lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
    }
}
static void rebuild(void) {
    dirty = false;
    lv_obj_clean(surface);
    timer_text = value_text = arc = error_text = create_button = NULL;
    alarm_list = NULL;
    if (page == 1) {
        title("Timer");
        if (chronvs_timer_running()) {
            timer_text = chronvs_aion_label(surface, "", 150, &lv_font_montserrat_48);
            chronvs_aion_label(surface, "EM CONTAGEM", 112, &lv_font_montserrat_12);
            chronvs_aion_action(surface, "Cancelar", 0, 264, CHRONVS_UI_ACTION_WIDTH, true, timer_action, 0);
        } else {
            const unsigned values[] = {1,5,10,15,30,60,120};
            const char *texts[] = {"1", "5", "10", "15", "30", "60", "120"};
            for (unsigned i = 0; i < 7; ++i)
                chronvs_aion_circle(surface, texts[i], i, 108, timer_action, values[i]);
        }
        previous_remaining = UINT32_MAX;
        previous_running = chronvs_timer_running();
        return;
    }
    if (view == LIST) {
        title("Alarmes");
        unsigned count = 0;
        for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) if (chronvs_alarm_get(i)) ++count;
        if (!count) chronvs_aion_label(surface, "Nenhum alarme", 170, &lv_font_montserrat_18);
        lv_obj_t *list = lv_obj_create(surface);
        alarm_list = list;
        lv_obj_remove_style_all(list);
        lv_obj_set_size(list, 260, 205);
        lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 88);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
        unsigned row = 0;
        for (unsigned i = 0; i < CHRONVS_ALARM_LIMIT; ++i) {
            const chronvs_alarm_t *alarm = chronvs_alarm_get(i);
            if (!alarm) continue;
            char text[24];
            snprintf(text, sizeof(text), "%02u:%02u", alarm->hour, alarm->minute);
            chronvs_aion_button(list, text, 0, row++ * 64, 248, 56, open_alarm, i);
        }
        lv_obj_t *new_button = chronvs_aion_action(surface, "Novo alarme", 0, 314, 180, false, open_alarm, -1);
        if (count == CHRONVS_ALARM_LIMIT) {
            lv_obj_add_state(new_button, LV_STATE_DISABLED);
            chronvs_aion_label(surface, "Limite de 12 alarmes", 370, &lv_font_montserrat_12);
        }
    } else if (view == HOUR || view == MINUTE) {
        title(view == HOUR ? "Escolha a hora" : "Escolha o minuto");
        arc = lv_arc_create(surface);
        lv_obj_set_size(arc, 260, 260);
        lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 82);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 0, view == HOUR ? 23 : 59);
        lv_arc_set_value(arc, view == HOUR ? hour : minute);
        chronvs_ui_style_arc(arc);
        lv_obj_add_event_cb(arc, arc_changed, LV_EVENT_VALUE_CHANGED, NULL);
        value_text = chronvs_aion_label(surface, "", 176, &lv_font_montserrat_48);
        chronvs_aion_label(surface, view == HOUR ? "00 - 23" : "00 - 59", 238, &lv_font_montserrat_12);
        chronvs_aion_action(surface, "Voltar", -66, 314, CHRONVS_UI_PAIR_WIDTH, true, back_button, 0);
        chronvs_aion_action(surface, "Proximo", 66, 314, CHRONVS_UI_PAIR_WIDTH, false, next_step, 0);
    } else {
        if (view == DETAIL) {
            const chronvs_alarm_t *alarm = chronvs_alarm_get(selected);
            if (!alarm) { view = LIST; dirty = true; return; }
            hour = alarm->hour; minute = alarm->minute; days = alarm->days;
        }
        char text[24];
        snprintf(text, sizeof(text), "%02u:%02u", hour, minute);
        title(text);
        make_days(view == DAYS);
        create_button = chronvs_aion_action(surface, view == DAYS ? "Criar" : "Excluir", 0,
                                            314, CHRONVS_UI_ACTION_WIDTH, view == DETAIL,
                                            view == DAYS ? next_step : delete_alarm, 0);
        if (view == DAYS && !days) lv_obj_add_state(create_button, LV_STATE_DISABLED);
    }
}

void chronvs_aion_pages_init(lv_obj_t *parent) {
    surface = lv_obj_create(parent);
    chronvs_aion_surface(surface);
    lv_obj_add_flag(surface, LV_OBJ_FLAG_HIDDEN);
}
void chronvs_aion_pages_show(unsigned next) {
    page = next;
    if (!page) { lv_obj_add_flag(surface, LV_OBJ_FLAG_HIDDEN); return; }
    view = LIST;
    lv_obj_clear_flag(surface, LV_OBJ_FLAG_HIDDEN);
    dirty = true;
}
bool chronvs_aion_pages_back(void) {
    if (page != 2 || view == LIST) return false;
    if (view == MINUTE) { minute = lv_arc_get_value(arc); view = HOUR; }
    else if (view == DAYS) view = MINUTE;
    else view = LIST;
    dirty = true;
    return true;
}
bool chronvs_aion_pages_editing(void) { return page == 2 && view != LIST; }
bool chronvs_aion_pages_can_swipe_back(lv_obj_t *target) {
    /* Capture this at press time: scrolling back to the top must not also
     * leave the page in the same contact. Outside the list, always allow it. */
    if (page != 2 || view != LIST) return true;
    for (lv_obj_t *obj = target; obj; obj = lv_obj_get_parent(obj)) {
        if (obj == alarm_list) return lv_obj_get_scroll_y(alarm_list) <= 0;
    }
    return true;
}
void chronvs_aion_pages_refresh(void) {
    if (!page || chronvs_system_ui_display_is_off()) return;
    if (page == 1 && previous_running != chronvs_timer_running()) dirty = true;
    if (dirty) rebuild();
    if (timer_text) {
        unsigned remaining = chronvs_timer_remaining();
        if (remaining != previous_remaining) {
            previous_remaining = remaining;
            lv_label_set_text_fmt(timer_text, "%02u:%02u", remaining / 60, remaining % 60);
        }
    }
    if (arc && value_text) {
        unsigned value = lv_arc_get_value(arc);
        char text[16];
        snprintf(text, sizeof(text), "%02u:%02u", view == HOUR ? value : hour, view == MINUTE ? value : minute);
        if (strcmp(lv_label_get_text(value_text), text)) lv_label_set_text(value_text, text);
    }
}
