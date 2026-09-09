#include "apps/app_catalog.h"
#include "services/mnemo_service.h"
#include "ui/control_style.h"
#include "ui/mnemo_font.h"
#include "ui/system_ui.h"
#include "ui/app_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *root, *page, *textarea, *status, *keyboard, *shift_key, *dialog;
static lv_timer_t *refresh;
static mnemo_editor_t editor;
static int slot = -1;
static bool dirty, save_error, reading, active, cursor_visible, was_off, symbols;
static uint32_t changed_at, blink_at;
static lv_point_t text_start;
static int start_scroll;
static bool text_dragged;

static void show_list(void);
static void open_editor(int index);
static void update_editor(bool text_changed);
static void back(void);
static chronvs_ui_app_input_t input_state = {.back = back};
static void update_keys(void);

/* Keep composition feedback local: no change to the global LVGL label layout. */
static void draw_pending(lv_event_t *event) {
    if (editor.pending_key < 0 || !editor.cursor || reading) return;
    lv_obj_t *text_label = lv_event_get_target(event);
    lv_point_t position;
    lv_label_get_letter_pos(text_label, editor.cursor - 1, &position);
    lv_area_t area; lv_obj_get_coords(text_label, &area);
    size_t offset = mnemo_text_offset(editor.text, editor.cursor - 1);
    const unsigned char *text = (const unsigned char *)editor.text + offset;
    uint32_t character = text[0] < 128 ? text[0] : ((text[0] & 31u) << 6) | (text[1] & 63u);
    lv_coord_t width = lv_font_get_glyph_width(&chronvs_mnemo_font, character, 0);
    lv_point_t start = {area.x1 + position.x, area.y1 + position.y + 23};
    lv_point_t end = {start.x + (width > 1 ? width - 1 : 1), start.y};
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(CHRONVS_UI_ACCENT); line.width = 2;
    lv_draw_line(lv_event_get_draw_ctx(event), &line, &start, &end);
}

/* LVGL clips scrolled children to the outer object, including its padding.
 * Restrict this label to the text viewport so previous lines cannot show
 * through the top inset of the editor. */
static void text_draw_event(lv_event_t *event) {
    static lv_area_t clip;
    static const lv_area_t *original;
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_DRAW_MAIN_BEGIN && code != LV_EVENT_DRAW_MAIN_END) return;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
        lv_area_t content;
        lv_obj_get_content_coords(textarea, &content);
        original = ctx->clip_area;
        if (!_lv_area_intersect(&clip, original, &content))
            clip = (lv_area_t){0, 0, -1, -1};
        ctx->clip_area = &clip;
    } else {
        draw_pending(event);
        ctx->clip_area = original;
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_color(obj, lv_color_hex(CHRONVS_UI_TEXT), 0);
    lv_label_set_text(obj, text);
    if (width) lv_obj_set_width(obj, width);
    lv_obj_set_pos(obj, x, y);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                        lv_event_cb_t cb, intptr_t value, bool outline) {
    lv_obj_t *obj = lv_btn_create(parent);
    chronvs_ui_style_control(obj, outline);
    lv_obj_set_size(obj, w, h); lv_obj_set_pos(obj, x, y);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, (void *)value);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text); lv_obj_center(caption);
    return obj;
}
static void new_page(void) {
    if (page) lv_obj_del(page);
    textarea = status = keyboard = shift_key = dialog = NULL;
    page = lv_obj_create(root); lv_obj_remove_style_all(page);
    lv_obj_set_size(page, 412, 412);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(page, "Mnemo", 140, 34, 132);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
}
static bool save_note(void) {
    mnemo_editor_confirm(&editor);
    if (!dirty || slot < 0) return true;
    save_error = !chronvs_mnemo_save((unsigned)slot, editor.text);
    if (!save_error) dirty = false;
    return !save_error;
}
static void back(void) {
    if (dialog) { lv_obj_del(dialog); dialog = NULL; return; }
    if (slot < 0) { chronvs_app_open("apps"); return; }
    if (!save_note()) { update_editor(false); return; }
    slot = -1; show_list();
}
static void row_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (index < 0) index = chronvs_mnemo_free_slot();
    if (index >= 0) open_editor(index);
}
static void show_list(void) {
    new_page();
    bool ready = chronvs_mnemo_init();
    lv_obj_t *list = lv_obj_create(page); lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 66, 90); lv_obj_set_size(list, 280, 210);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    unsigned order[MNEMO_NOTE_LIMIT], count = 0;
    for (unsigned i = 0; i < MNEMO_NOTE_LIMIT; ++i) {
        const mnemo_note_t *note = chronvs_mnemo_get(i); if (!note) continue;
        unsigned j = count++;
        while (j && chronvs_mnemo_get(order[j-1])->revision < note->revision) {
            order[j] = order[j-1]; --j;
        }
        order[j] = i;
    }
    for (unsigned i = 0; i < count; ++i) {
        const char *text = chronvs_mnemo_get(order[i])->text;
        char title[90]; size_t n = mnemo_text_offset(text, 36);
        memcpy(title, text, n); title[n] = 0;
        char *newline = strchr(title, '\n'); if (newline) *newline = 0;
        lv_obj_t *row = button(list, *title ? title : "Nota", 0, (int)i*64, 280, 56,
                               row_event, order[i], false);
        lv_obj_t *caption = lv_obj_get_child(row, 0);
        lv_obj_set_style_text_font(caption, &chronvs_mnemo_font, 0);
        lv_obj_set_width(caption, 244); lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
        lv_obj_center(caption);
    }
    if (!count) {
        lv_obj_t *empty = label(list, ready ? "Suas ideias, aqui." : "Falha ao abrir notas", 0, 75, 280);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *create = button(page, "Nova nota", 116, 314, 180, 54, row_event, -1, false);
    if (!ready || count == MNEMO_NOTE_LIMIT) lv_obj_add_state(create, LV_STATE_DISABLED);
    char count_text[32]; snprintf(count_text, sizeof(count_text), "%u / %u notas", count, MNEMO_NOTE_LIMIT);
    lv_obj_t *counter = label(page, count_text, 136, 376, 140);
    lv_obj_set_style_text_font(counter, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(counter, LV_TEXT_ALIGN_CENTER, 0);
    chronvs_ui_app_input_bind(page, &input_state);
}
static void update_editor(bool text_changed) {
    if (!textarea || chronvs_system_ui_display_is_off()) return;
    bool follow_cursor = text_changed || lv_textarea_get_cursor_pos(textarea) != editor.cursor;
    if (text_changed) lv_textarea_set_text(textarea, editor.text);
    lv_textarea_set_cursor_pos(textarea, editor.cursor);
    if (follow_cursor) {
        lv_obj_update_layout(textarea);
        lv_point_t position;
        lv_label_get_letter_pos(lv_textarea_get_label(textarea), editor.cursor, &position);
        int scroll = lv_obj_get_scroll_y(textarea);
        int height = lv_obj_get_content_height(textarea);
        if (position.y < scroll) scroll = position.y;
        else if (position.y + 24 > scroll + height)
            scroll = ((position.y + 24 - height + 24) / 25) * 25;
        lv_obj_scroll_to_y(textarea, scroll, LV_ANIM_OFF);
    }
    char message[64];
    snprintf(message, sizeof(message), "%s  %u/%u", save_error ? "Falha ao salvar" :
             dirty ? "Salvando" : "Salvo", mnemo_text_length(editor.text), MNEMO_MAX_CHARS);
    lv_label_set_text(status, message);
    if (editor.shift || (editor.pending_key >= 0 && editor.pending_shift)) lv_obj_add_state(shift_key, LV_STATE_CHECKED);
    else lv_obj_clear_state(shift_key, LV_STATE_CHECKED);
    update_keys();
    lv_obj_invalidate(lv_textarea_get_label(textarea));
    cursor_visible = true; blink_at = lv_tick_get();
    lv_obj_set_style_opa(textarea, reading ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_CURSOR);
}
static void changed(void) {
    dirty = true; save_error = false; changed_at = lv_tick_get(); update_editor(true);
}
static void key_event(lv_event_t *event) {
    unsigned key = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_SHORT_CLICKED) return;
    if (symbols && key < MNEMO_LETTER_KEYS) key += MNEMO_SYMBOL_FIRST;
    if (mnemo_editor_key(&editor, key, lv_tick_get())) changed();
}
static void text_event(lv_event_t *event) {
    lv_indev_t *input = lv_indev_get_act(); if (!input) return;
    lv_point_t point; lv_indev_get_point(input, &point);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        mnemo_editor_confirm(&editor); text_start = point;
        start_scroll = lv_obj_get_scroll_y(textarea); text_dragged = false;
        update_editor(false);
        lv_obj_scroll_to_y(textarea, start_scroll, LV_ANIM_OFF);
    } else if (code == LV_EVENT_PRESSING) {
        if (abs(point.x-text_start.x) > 10 || abs(point.y-text_start.y) > 10) text_dragged = true;
    } else if (code == LV_EVENT_RELEASED) {
        if (!text_dragged && !reading) {
            lv_obj_t *text_label = lv_textarea_get_label(textarea);
            lv_area_t coords; lv_obj_get_coords(text_label, &coords);
            lv_point_t relative = {point.x-coords.x1, point.y-coords.y1};
            mnemo_editor_move(&editor, lv_label_get_letter_on(text_label, &relative)); update_editor(false);
        }
    }
}
static void toolbar_event(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (code != LV_EVENT_SHORT_CLICKED && !(action == 0 && code == LV_EVENT_LONG_PRESSED_REPEAT)) return;
    if (action == 0) { if (mnemo_editor_backspace(&editor)) changed(); }
    else if (action == 1) { if (mnemo_editor_insert(&editor, "\n")) changed(); }
    else if (action == 2) {
        mnemo_editor_confirm(&editor); reading = !reading;
        if (reading) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(textarea, reading ? 218 : 112);
        update_editor(true);
    } else if (action == 3) {
        mnemo_editor_shift(&editor); update_editor(false);
    } else if (action == 4) {
        if (mnemo_editor_insert(&editor, " ")) changed();
    } else if (action == 5) {
        mnemo_editor_confirm(&editor); symbols = !symbols; update_editor(false);
    }
}
static void delete_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
    int action = (int)(intptr_t)lv_event_get_user_data(event);
    if (action == 1) {
        if (!chronvs_mnemo_save((unsigned)slot, "")) {
            lv_obj_t *message = lv_obj_get_child(dialog, 0);
            lv_label_set_text(message, "Falha ao excluir"); return;
        }
        dirty = false; slot = -1; show_list(); return;
    }
    if (action == 2) { lv_obj_del(dialog); dialog = NULL; return; }
    mnemo_editor_confirm(&editor);
    dialog = lv_obj_create(page); lv_obj_remove_style_all(dialog); lv_obj_set_size(dialog, 412, 412);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(CHRONVS_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_t *message = label(dialog, "Excluir esta nota?", 76, 148, 260);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    button(dialog, "Cancelar", 80, 228, 120, 54, delete_event, 2, true);
    button(dialog, "Excluir", 212, 228, 120, 54, delete_event, 1, false);
    chronvs_ui_app_input_bind(dialog, &input_state);
}
static void open_editor(int index) {
    slot = index; const mnemo_note_t *note = chronvs_mnemo_get((unsigned)index);
    mnemo_editor_load(&editor, note ? note->text : "");
    dirty = save_error = reading = symbols = false;
    new_page();
    button(page, LV_SYMBOL_TRASH, 286, 42, 36, 36, delete_event, 0, true);
    button(page, LV_SYMBOL_KEYBOARD, 90, 42, 36, 36, toolbar_event, 2, true);
    status = label(page, "", 110, 66, 192);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    textarea = lv_textarea_create(page); lv_obj_remove_style_all(textarea);
    lv_obj_set_pos(textarea, 66, 82); lv_obj_set_size(textarea, 280, 112);
    lv_obj_set_style_pad_all(textarea, 6, 0);
    lv_obj_set_style_text_font(textarea, &chronvs_mnemo_font, 0);
    lv_obj_set_style_text_color(textarea, lv_color_hex(CHRONVS_UI_TEXT), 0);
    lv_obj_set_style_text_line_space(textarea, 1, 0);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_border_color(textarea, lv_color_hex(CHRONVS_UI_SURFACE), 0);
    lv_obj_set_style_radius(textarea, 10, 0);
    lv_obj_set_style_border_width(textarea, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(textarea, lv_color_hex(CHRONVS_UI_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_anim_time(textarea, 0, LV_PART_CURSOR);
    lv_obj_t *text_label = lv_textarea_get_label(textarea);
    lv_obj_add_event_cb(text_label, text_draw_event, LV_EVENT_ALL, NULL);
    lv_textarea_set_cursor_click_pos(textarea, false);
    lv_obj_set_scroll_dir(textarea, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(textarea, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(textarea, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(textarea, text_event, LV_EVENT_ALL, NULL);
    keyboard = lv_obj_create(page); lv_obj_remove_style_all(keyboard);
    lv_obj_set_pos(keyboard, 54, 204); lv_obj_set_size(keyboard, 304, 188);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < MNEMO_LETTER_KEYS; ++i) {
        int row = i / 6;
        int column = i % 6;
        lv_obj_t *key = button(keyboard, "", (row == 2 ? 26 : 0)+column*52, row*48,
                               44, 44, key_event, i, false);
        lv_obj_t *caption = lv_obj_get_child(key, 0);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_18, 0);
        lv_obj_t *sub = lv_label_create(key);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_label_set_text(sub, "");
    }
    shift_key = button(keyboard, LV_SYMBOL_UP, 78, 96, 44, 44, toolbar_event, 3, true);
    lv_obj_t *punctuation = button(keyboard, ".,?!", 130, 96, 44, 44, key_event, 13, false);
    lv_obj_set_style_text_font(punctuation, &lv_font_montserrat_14, 0);
    lv_obj_t *social = button(keyboard, "@#&", 182, 96, 44, 44, key_event, 14, false);
    lv_obj_set_style_text_font(social, &lv_font_montserrat_14, 0);
    button(keyboard, LV_SYMBOL_BACKSPACE, 234, 96, 44, 44, toolbar_event, 0, true);
    button(keyboard, "_", 78, 144, 44, 44, toolbar_event, 4, true);
    button(keyboard, LV_SYMBOL_NEW_LINE, 130, 144, 44, 44, toolbar_event, 1, true);
    lv_obj_t *mode = button(keyboard, "#+=", 182, 144, 44, 44, toolbar_event, 5, true);
    lv_obj_set_style_text_font(mode, &lv_font_montserrat_14, 0);
    update_editor(true);
    chronvs_ui_app_input_bind(page, &input_state);
}
static void update_keys(void) {
    static const char *letters[13] = {"ab", "cd", "ef", "gh", "ij", "kl", "mn", "op", "qr", "st", "uv", "wx", "yz"};
    static const char *upper[13] = {"AB", "CD", "EF", "GH", "IJ", "KL", "MN", "OP", "QR", "ST", "UV", "WX", "YZ"};
    static const char *marks[13] = {".,", "?!", ":;", "\"'", "()", "[]", "{}", "<>", "+-=", "*/\\", "_@", "#$%", "&|~^`"};
    bool shifted = editor.shift || (editor.pending_key >= 0 && editor.pending_shift);
    for (unsigned i = 0; i < MNEMO_LETTER_KEYS; ++i) {
        lv_obj_t *key = lv_obj_get_child(keyboard, i);
        lv_obj_t *caption = lv_obj_get_child(key, 0), *sub = lv_obj_get_child(key, 1);
        const char *text = symbols ? marks[i] : shifted ? upper[i] : letters[i];
        if (strcmp(lv_label_get_text(caption), text)) lv_label_set_text(caption, text);
        lv_obj_set_style_text_font(caption, symbols ? &lv_font_montserrat_12 : &lv_font_montserrat_18, 0);
        lv_obj_align(caption, !symbols && i >= 3 ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, !symbols && i >= 3 ? 3 : 0);
        char number[2] = {i == 12 ? '0' : (char)('1'+(int)i-3), 0};
        const char *digit = !symbols && i >= 3 ? number : "";
        if (strcmp(lv_label_get_text(sub), digit)) lv_label_set_text(sub, digit);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -3);
    }
    lv_obj_t *mode = lv_obj_get_child(lv_obj_get_child(keyboard, 19), 0);
    const char *mode_text = symbols ? "abc" : "#+=";
    if (strcmp(lv_label_get_text(mode), mode_text)) lv_label_set_text(mode, mode_text);
}
static void poll(lv_timer_t *timer) {
    (void)timer;
    if (!active || slot < 0) return;
    uint32_t now = lv_tick_get();
    bool expired = mnemo_editor_expire(&editor, now);
    bool saved = false;
    if (dirty && (uint32_t)(now-changed_at) >= (save_error ? 10000u : 1500u)) {
        save_note(); changed_at = now; saved = true;
    }
    if (chronvs_system_ui_display_is_off()) { was_off = true; return; }
    if (expired || saved || was_off) { update_editor(false); was_off = false; }
    if (!reading && !dialog && (uint32_t)(now-blink_at) >= 500) {
        cursor_visible = !cursor_visible; blink_at = now;
        lv_obj_set_style_opa(textarea, cursor_visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_CURSOR);
    }
}
static void create_icon(lv_obj_t *parent) {
    lv_obj_t *paper = lv_obj_create(parent); lv_obj_remove_style_all(paper);
    lv_obj_set_size(paper, 28, 36); lv_obj_center(paper);
    lv_obj_set_style_radius(paper, 4, 0);
    lv_obj_set_style_border_width(paper, 2, 0);
    lv_obj_set_style_border_color(paper, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    lv_obj_clear_flag(paper, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *line = lv_obj_create(paper); lv_obj_remove_style_all(line);
        lv_obj_set_pos(line, 5, 8+i*7); lv_obj_set_size(line, i == 2 ? 9 : 16, 2);
        lv_obj_set_style_bg_color(line, lv_color_hex(CHRONVS_UI_TEXT), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}
static lv_obj_t *create(lv_obj_t *parent) {
    root = lv_obj_create(parent); lv_obj_remove_style_all(root); lv_obj_set_size(root, 412, 412);
    lv_obj_set_style_bg_color(root, lv_color_hex(CHRONVS_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    show_list(); refresh = lv_timer_create(poll, 50, NULL); lv_timer_pause(refresh);
    return root;
}
static void show(void) {
    active = true; lv_timer_resume(refresh);
    if (slot >= 0) update_editor(true); else show_list();
}
static void hide(void) {
    save_note(); active = false; lv_timer_pause(refresh);
}
const chronvs_app_t chronvs_mnemo_app = {
    .id = "mnemo", .name = "Mnemo", .create_icon = create_icon, .launcher_visible = true,
    .create = create, .on_show = show, .on_hide = hide,
};
CHRONVS_REGISTER_APP(chronvs_mnemo_app)
