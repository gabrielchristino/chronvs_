#include "apps/hemera_reminders.h"
#include "services/aion_service.h"
#include "core/mnemo_text.h"
#include "core/calendar.h"
#include "ui/aion_widgets.h"
#include "ui/app_input.h"
#include "ui/mnemo_font.h"
#include "ui/system_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef enum { LIST, TITLE, HOUR, MINUTE, REVIEW, ITEM, DELETE, CANCEL } view_t;
static view_t view, cancel_from;
static lv_obj_t *root, *page, *text, *status, *keyboard, *arc, *value, *list;
static chronvs_reminder_t draft;
static mnemo_editor_t editor;
static unsigned selected;
static bool symbols;
static lv_point_t text_start;
static bool dragged;
static uint32_t refresh_tick;
static uint32_t revision;
static void rebuild(void);
static void back(void);
static void vertical(int direction);
static chronvs_ui_app_input_t input = {.back=back, .vertical=vertical};
/* Arcs own their drags; text owns vertical scrolling but still supports back. */
static chronvs_ui_app_input_t arc_input;
static chronvs_ui_app_input_t text_input = {.back=back};

static lv_obj_t *label(const char *caption, int y, int width, const lv_font_t *font) {
    lv_obj_t *obj = chronvs_aion_label(page, caption, y, font);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}
static void date_label(void) {
    char date[24];
    snprintf(date, sizeof(date), "%02u/%02u/%04u", draft.day, draft.month, 2000u+draft.year);
    label(date, 58, 180, &lv_font_montserrat_18);
}
static void error(const char *message) { lv_label_set_text(status, message); }
static void update_title(void) {
    lv_textarea_set_text(text, editor.text);
    lv_textarea_set_cursor_pos(text, editor.cursor);
    char count[24]; snprintf(count, sizeof(count), "%u / %u", mnemo_text_length(editor.text), CHRONVS_REMINDER_CHARS);
    error(count);
    const char *letters[] = {"ab", "cd", "ef", "gh", "ij", "kl", "mn", "op", "qr", "st", "uv", "wx", "yz"};
    const char *upper[] = {"AB", "CD", "EF", "GH", "IJ", "KL", "MN", "OP", "QR", "ST", "UV", "WX", "YZ"};
    const char *marks[] = {".,", "?!", ":;", "\"'", "()", "[]", "{}", "<>", "+-=", "*/\\", "_@", "#$%", "&|~^`"};
    bool shift = editor.shift || (editor.pending_key >= 0 && editor.pending_shift);
    for (unsigned i=0; i<13; ++i) {
        lv_obj_t *key = lv_obj_get_child(keyboard, i);
        lv_obj_t *caption = lv_obj_get_child(key, 0);
        lv_label_set_text(caption, symbols ? marks[i] : shift ? upper[i] : letters[i]);
        lv_obj_set_style_text_font(caption, symbols ? &lv_font_montserrat_12 : &lv_font_montserrat_18, 0);
        lv_obj_align(caption, !symbols && i>=3 ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, !symbols && i>=3 ? 3 : 0);
        char digit[2] = {i==12 ? '0' : '1'+(int)i-3, 0};
        lv_obj_t *sub = lv_obj_get_child(key, 1);
        lv_label_set_text(sub, !symbols && i>=3 ? digit : "");
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -3);
    }
    lv_obj_t *shift_key = lv_obj_get_child(keyboard, 13);
    if (shift) lv_obj_add_state(shift_key, LV_STATE_CHECKED);
    else lv_obj_clear_state(shift_key, LV_STATE_CHECKED);
    lv_label_set_text(lv_obj_get_child(lv_obj_get_child(keyboard, 19), 0), symbols ? "abc" : "#+=");
}
static bool accept_title(void) {
    mnemo_editor_confirm(&editor);
    if (!editor.text[strspn(editor.text, " ")]) { error("Escreva um título"); return false; }
    strcpy(draft.title, editor.text);
    return true;
}
static void vertical(int direction) {
    if (view == TITLE && direction > 0) {
        if (!accept_title()) return;
        view = HOUR;
    } else if (view == HOUR) {
        draft.hour = lv_arc_get_value(arc);
        view = direction > 0 ? MINUTE : TITLE;
    } else if (view == MINUTE) {
        draft.minute = lv_arc_get_value(arc);
        view = direction > 0 ? REVIEW : HOUR;
    } else if (view == REVIEW && direction < 0) view = MINUTE;
    else return;
    rebuild();
}
static void back(void) {
    if (view == LIST) { lv_obj_del(root); root = page = NULL; return; }
    if (view == TITLE) { cancel_from = view; view = CANCEL; }
    else if (view == HOUR) { draft.hour = lv_arc_get_value(arc); view = TITLE; }
    else if (view == MINUTE) { draft.minute = lv_arc_get_value(arc); view = HOUR; }
    else if (view == REVIEW) view = MINUTE;
    else if (view == DELETE) view = ITEM;
    else if (view == CANCEL) view = cancel_from;
    else view = LIST;
    rebuild();
}
static void action(lv_event_t *event) {
    int id = (intptr_t)lv_event_get_user_data(event);
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    if (id == -1) {
        chronvs_time_t now;
        if (!chronvs_aion_time(&now)) { error("Horário indisponível"); return; }
        draft.hour = now.hour; draft.minute = (now.minute + 1) % 60;
        if (!draft.minute) draft.hour = (now.hour + 1) % 24;
        draft.title[0] = 0; draft.done = 0;
        mnemo_editor_load(&editor, ""); symbols = false; view = TITLE;
    } else if (id >= 0) { selected = id; view = ITEM; }
    else if (id == -2) {
        chronvs_time_t now;
        if (!chronvs_aion_time(&now)) { error("Horario indisponivel"); return; }
        int date=chronvs_calendar_ordinal(2000+draft.year,draft.month,draft.day);
        int today=chronvs_calendar_ordinal(2000+now.year,now.month,now.day);
        if (date<today || (date==today && draft.hour*60+draft.minute<=now.hour*60+now.minute)) {
            error("Use um horario futuro"); return;
        }
        if (!chronvs_reminder_create(&draft)) { error("Falha ao salvar"); return; }
        view = LIST;
    } else if (id == -3) view = DELETE;
    else if (id == -4) {
        if (!chronvs_reminder_delete(selected)) { error("Falha ao excluir"); return; }
        view = LIST;
    } else if (id == -5) { back(); return; }
    else if (id == -6) view = LIST;
    rebuild();
}
static void key_event(lv_event_t *event) {
    unsigned key = (uintptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_SHORT_CLICKED && !(key == 31 && code == LV_EVENT_LONG_PRESSED_REPEAT)) return;
    if (key == 33) { vertical(1); return; }
    if (key == 30) mnemo_editor_shift(&editor);
    else if (key == 31) mnemo_editor_backspace(&editor);
    else if (key == 34) { mnemo_editor_confirm(&editor); symbols = !symbols; }
    else {
        unsigned group = symbols && key < 13 ? key + MNEMO_SYMBOL_FIRST : key;
        if (mnemo_text_length(editor.text) >= CHRONVS_REMINDER_CHARS &&
            (key == 32 || editor.pending_key != (int)group || lv_tick_elaps(editor.last_tap) >= MNEMO_TAP_MS)) {
            error("Limite de 40 caracteres"); return;
        }
        if (key == 32) mnemo_editor_insert(&editor, " ");
        else mnemo_editor_key(&editor, group, lv_tick_get());
    }
    update_title();
}
static lv_obj_t *key(const char *caption, int x, int y, unsigned id) {
    lv_obj_t *obj = lv_btn_create(keyboard);
    chronvs_ui_style_control(obj, id >= 30);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, 44, 44);
    lv_obj_add_event_cb(obj, key_event, LV_EVENT_ALL, (void *)(uintptr_t)id);
    lv_obj_t *caption_obj = lv_label_create(obj);
    lv_obj_set_style_text_font(caption_obj, &lv_font_montserrat_14, 0);
    lv_label_set_text(caption_obj, caption); lv_obj_center(caption_obj);
    return obj;
}
static void text_event(lv_event_t *event) {
    lv_indev_t *indev = lv_indev_get_act(); if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) { text_start = p; dragged = false; }
    if (code == LV_EVENT_PRESSING && (abs(p.x-text_start.x)>10 || abs(p.y-text_start.y)>10)) dragged = true;
    if (code == LV_EVENT_RELEASED && !dragged) {
        lv_obj_t *caption = lv_textarea_get_label(text);
        lv_area_t a; lv_obj_get_coords(caption, &a);
        lv_point_t relative = {p.x-a.x1, p.y-a.y1};
        mnemo_editor_move(&editor, lv_label_get_letter_on(caption, &relative));
        update_title();
    }
}
static void title_page(void) {
    label("Título do lembrete", 48, 230, &chronvs_mnemo_font);
    status = label("", 76, 230, &chronvs_mnemo_font);
    text = lv_textarea_create(page); lv_obj_remove_style_all(text);
    lv_obj_set_pos(text, 66, 108); lv_obj_set_size(text, 280, 84);
    lv_obj_set_style_pad_all(text, 6, 0);
    lv_obj_set_style_text_font(text, &chronvs_mnemo_font, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(CHRONVS_UI_TEXT), 0);
    lv_obj_set_style_border_width(text, 1, 0);
    lv_obj_set_style_border_color(text, lv_color_hex(CHRONVS_UI_SURFACE), 0);
    lv_obj_set_style_border_width(text, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(text, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(text, lv_color_hex(CHRONVS_UI_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_anim_time(text, 0, LV_PART_CURSOR);
    lv_textarea_set_cursor_click_pos(text, false);
    lv_obj_set_scroll_dir(text, LV_DIR_VER);
    lv_obj_add_event_cb(text, text_event, LV_EVENT_ALL, NULL);
    keyboard = lv_obj_create(page); lv_obj_remove_style_all(keyboard);
    lv_obj_set_pos(keyboard, 54, 204); lv_obj_set_size(keyboard, 304, 188);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i=0; i<13; ++i) {
        lv_obj_t *obj = key("", (i/6==2 ? 26 : 0)+(i%6)*52, (i/6)*48, i);
        lv_obj_t *sub = lv_label_create(obj);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    }
    key(LV_SYMBOL_UP, 78, 96, 30);
    key(".,?!", 130, 96, 13); key("@#&", 182, 96, 14);
    key(LV_SYMBOL_BACKSPACE, 234, 96, 31);
    key("_", 78, 144, 32); key(LV_SYMBOL_OK, 130, 144, 33); key("#+=", 182, 144, 34);
    update_title();
}
static void rebuild(void) {
    revision=chronvs_reminder_revision();
    if (page) lv_obj_del(page);
    page = lv_obj_create(root); chronvs_aion_surface(page);
    chronvs_ui_app_input_bind(page, &input);
    text = keyboard = arc = value = status = list = NULL;
    if (view == TITLE) title_page();
    else {
        date_label();
        status = label("", 370, 220, &lv_font_montserrat_12);
        if (view == LIST) {
            list = lv_obj_create(page); lv_obj_remove_style_all(list);
            lv_obj_set_size(list, 280, 206); lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 98);
            lv_obj_set_scroll_dir(list, LV_DIR_VER);
            unsigned count=0, total=0, order[CHRONVS_REMINDER_LIMIT];
            for (unsigned i=0; i<CHRONVS_REMINDER_LIMIT; ++i) {
                const chronvs_reminder_t *r = chronvs_reminder_get(i); if (!r) continue;
                ++total;
                if (r->year!=draft.year || r->month!=draft.month || r->day!=draft.day) continue;
                unsigned j=count++;
                while (j) {
                    const chronvs_reminder_t *prev=chronvs_reminder_get(order[j-1]);
                    if (prev->hour*60+prev->minute <= r->hour*60+r->minute) break;
                    order[j]=order[j-1]; --j;
                }
                order[j]=i;
            }
            for (unsigned j=0; j<count; ++j) {
                const chronvs_reminder_t *r=chronvs_reminder_get(order[j]);
                char caption[112]; snprintf(caption, sizeof(caption), "%02u:%02u %s%s", r->hour, r->minute,
                    r->done ? LV_SYMBOL_OK " " : "", r->title);
                lv_obj_t *row=chronvs_aion_button(list, caption, 0, j*64, 276, 54, action, order[j]);
                lv_obj_t *name=lv_obj_get_child(row,0); lv_obj_set_width(name,244);
                lv_obj_set_style_text_font(name,&chronvs_mnemo_font,0);
                lv_label_set_long_mode(name,LV_LABEL_LONG_DOT); lv_obj_center(name);
            }
            if (!count) label("Nenhum lembrete", 180, 260, &chronvs_mnemo_font);
            lv_obj_t *add=chronvs_aion_action(page,"Criar",0,314,CHRONVS_UI_ACTION_WIDTH,false,action,-1);
            if (total==CHRONVS_REMINDER_LIMIT) {
                lv_obj_add_state(add, LV_STATE_DISABLED); error("Limite de 12 lembretes");
            }
        } else if (view==HOUR || view==MINUTE) {
            label(view==HOUR ? "Escolha a hora" : "Escolha o minuto", 92, 240, &chronvs_mnemo_font);
            arc=lv_arc_create(page); lv_obj_set_size(arc,240,240); lv_obj_align(arc,LV_ALIGN_TOP_MID,0,122);
            lv_arc_set_rotation(arc,135); lv_arc_set_bg_angles(arc,0,270);
            lv_arc_set_range(arc,0,view==HOUR?23:59); lv_arc_set_value(arc,view==HOUR?draft.hour:draft.minute);
            chronvs_ui_style_arc(arc);
            value=label("",204,190,&lv_font_montserrat_48);
            lv_obj_add_flag(value, LV_OBJ_FLAG_CLICKABLE);
            label("Deslize para cima", 370, 200, &lv_font_montserrat_12);
        } else if (view==REVIEW || view==ITEM) {
            const chronvs_reminder_t *r=view==ITEM ? chronvs_reminder_get(selected) : &draft;
            if (!r) { view=LIST; rebuild(); return; }
            lv_obj_t *caption=label(r->title,116,280,&chronvs_mnemo_font);
            lv_label_set_long_mode(caption,LV_LABEL_LONG_WRAP);
            char time[12]; snprintf(time,sizeof(time),"%02u:%02u",r->hour,r->minute);
            label(time,218,200,&lv_font_montserrat_48);
            if (view==ITEM) label(r->done?"Concluído":"Agendado",280,220,&chronvs_mnemo_font);
            chronvs_aion_action(page,view==REVIEW?"Salvar":"Excluir",0,314,CHRONVS_UI_ACTION_WIDTH,
                view==ITEM,action,view==REVIEW?-2:-3);
        } else {
            label(view==DELETE?"Excluir este lembrete?":"Descartar rascunho?",148,280,&chronvs_mnemo_font);
            chronvs_aion_action(page,"Cancelar",-66,242,CHRONVS_UI_PAIR_WIDTH,true,action,-5);
            chronvs_aion_action(page,view==DELETE?"Excluir":"Descartar",66,242,CHRONVS_UI_PAIR_WIDTH,false,
                action,view==DELETE?-4:-6);
        }
    }
    /* Bind each subtree exactly once, with drag ownership chosen by control. */
    lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
    for (unsigned i=0; i<lv_obj_get_child_cnt(page); ++i) {
        lv_obj_t *child=lv_obj_get_child(page,i);
        chronvs_ui_app_input_bind(child, child==arc?&arc_input:(child==text || child==list)?&text_input:&input);
    }
    refresh_tick=lv_tick_get()-20;
    chronvs_hemera_reminders_poll();
}
void chronvs_hemera_reminders_open(lv_obj_t *parent,int year,int month,int day) {
    if (root) return;
    draft=(chronvs_reminder_t){.year=year-2000,.month=month,.day=day};
    root=lv_obj_create(parent); chronvs_aion_surface(root);
    view=LIST; rebuild();
}
bool chronvs_hemera_reminders_active(void) { return root!=NULL; }
void chronvs_hemera_reminders_poll(void) {
    if (!root || chronvs_system_ui_display_is_off() || lv_tick_elaps(refresh_tick)<20) return;
    refresh_tick=lv_tick_get();
    if (revision!=chronvs_reminder_revision() && (view==LIST || view==ITEM)) {
        rebuild(); return;
    }
    if (view==TITLE && mnemo_editor_expire(&editor,lv_tick_get())) update_title();
    if (arc && value) {
        char caption[12]; snprintf(caption,sizeof(caption),"%02u:%02u",view==HOUR?lv_arc_get_value(arc):draft.hour,
            view==MINUTE?lv_arc_get_value(arc):draft.minute);
        if (strcmp(lv_label_get_text(value),caption)) lv_label_set_text(value,caption);
    }
}
