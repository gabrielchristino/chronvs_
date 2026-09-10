#include "apps/app_catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "core/app_manager.h"
#include "core/calculator.h"
#include "ui/aion_widgets.h"
#include "ui/app_input.h"
#include "ui/control_style.h"
#include "ui/system_ui.h"

typedef enum { KEY_APPEND, KEY_CLEAR, KEY_DELETE, KEY_EVALUATE } key_action_t;

typedef struct {
    const char *label;
    const char *text;
    int x;
    int y;
    key_action_t action;
    bool accent;
} calculator_key_t;

static const calculator_key_t keys[] = {
    {"C",   NULL, -155, 148, KEY_CLEAR, false},
    {"(",   "(",  -155, 206, KEY_APPEND, false},
    {"DEL", NULL,  155, 148, KEY_DELETE, false},
    {")",   ")",   155, 206, KEY_APPEND, false},
    {"7", "7", -93, 148, KEY_APPEND, false},
    {"8", "8", -31, 148, KEY_APPEND, false},
    {"9", "9",  31, 148, KEY_APPEND, false},
    {"/", "/",  93, 148, KEY_APPEND, false},
    {"4", "4", -93, 206, KEY_APPEND, false},
    {"5", "5", -31, 206, KEY_APPEND, false},
    {"6", "6",  31, 206, KEY_APPEND, false},
    {"x", "*",  93, 206, KEY_APPEND, false},
    {"1", "1", -93, 264, KEY_APPEND, false},
    {"2", "2", -31, 264, KEY_APPEND, false},
    {"3", "3",  31, 264, KEY_APPEND, false},
    {"-", "-",  93, 264, KEY_APPEND, false},
    {"0", "0", -93, 322, KEY_APPEND, false},
    {".", ".", -31, 322, KEY_APPEND, false},
    {"=", NULL,  31, 322, KEY_EVALUATE, true},
    {"+", "+",  93, 322, KEY_APPEND, false},
};

static lv_obj_t *display;
static char expression[CHRONVS_CALCULATOR_TEXT_SIZE];
static const char *calculation_error;
static bool evaluated;
static void back(void);
static chronvs_ui_app_input_t input = {.back = back};

static void update_display(void) {
    const char *text = calculation_error ? calculation_error : expression;
    char tail[20];
    if (!calculation_error && strlen(text) > 19) {
        strcpy(tail, "...");
        strcat(tail, text + strlen(text) - 16);
        text = tail;
    }
    lv_label_set_text(display, *text ? text : "0");
    lv_obj_set_style_text_font(display,
        calculation_error ? &lv_font_montserrat_18 : &lv_font_montserrat_24, 0);
}

static void key_event(lv_event_t *event) {
    if (input.consumed || chronvs_system_ui_display_is_off()) return;
    const calculator_key_t *key = lv_event_get_user_data(event);
    calculation_error = NULL;
    size_t length = strlen(expression);

    switch (key->action) {
    case KEY_CLEAR:
        expression[0] = 0;
        evaluated = false;
        break;
    case KEY_DELETE:
        if (length) expression[length - 1] = 0;
        evaluated = false;
        break;
    case KEY_EVALUATE: {
        double result;
        if (chronvs_calculator_evaluate(expression, &result, &calculation_error)) {
            snprintf(expression, sizeof(expression), "%.12g", result == 0 ? 0.0 : result);
            evaluated = true;
        }
        break;
    }
    case KEY_APPEND:
        if (evaluated && (isdigit((unsigned char)key->text[0]) ||
                          key->text[0] == '.' || key->text[0] == '(')) {
            length = 0;
        }
        if (length + strlen(key->text) >= sizeof(expression)) {
            calculation_error = "Limite de 64 caracteres";
            break;
        }
        strcpy(expression + length, key->text);
        evaluated = false;
        break;
    }
    update_display();
}

static void draw_icon(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    const int x = area.x1 + (lv_area_get_width(&area) - 36) / 2;
    const int y = area.y1 + (lv_area_get_height(&area) - 36) / 2;
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    /* Open mathematical marks give this app a silhouette unlike a page. */
    static const lv_point_t strokes[][2] = {
        {{1, 8}, {15, 8}}, {{8, 1}, {8, 15}},       /* + */
        {{23, 8}, {35, 8}},                        /* - */
        {{3, 25}, {13, 35}}, {{3, 35}, {13, 25}},  /* x */
        {{23, 26}, {35, 26}}, {{23, 33}, {35, 33}},/* = */
    };
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 3;
    line.round_start = line.round_end = true;
    for (unsigned i = 0; i < sizeof(strokes) / sizeof(strokes[0]); ++i) {
        line.color = lv_color_hex(i < 2 || i >= 5 ? CHRONVS_UI_ACCENT : CHRONVS_UI_TEXT);
        lv_point_t start = {x + strokes[i][0].x, y + strokes[i][0].y};
        lv_point_t end = {x + strokes[i][1].x, y + strokes[i][1].y};
        lv_draw_line(context, &line, &start, &end);
    }
}

static void create_icon(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, draw_icon, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_invalidate(parent);
}

static void back(void) {
    chronvs_app_open("apps");
}

static lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    chronvs_aion_surface(root);

    display = chronvs_aion_label(root, "0", 44, &lv_font_montserrat_24);
    lv_obj_set_width(display, 240);
    lv_obj_set_height(display, 29);
    lv_obj_align(display, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_text_align(display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(display, LV_LABEL_LONG_CLIP);

    for (unsigned index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const calculator_key_t *key = &keys[index];
        lv_obj_t *button = chronvs_aion_button(root, key->label, key->x, key->y,
                                               54, 54, key_event, (intptr_t)key);
        if (key->accent) lv_obj_add_state(button, LV_STATE_CHECKED);
    }
    chronvs_ui_app_input_bind(root, &input);
    return root;
}

static void show(void) {
    expression[0] = 0;
    calculation_error = NULL;
    evaluated = false;
    input.consumed = false;
    update_display();
}

const chronvs_app_t chronvs_calculator_app = {
    .id = "calculator",
    .name = "Calculadora",
    .create_icon = create_icon,
    .launcher_visible = true,
    .create = create,
    .on_show = show,
    .on_hide = NULL,
};

CHRONVS_REGISTER_APP(chronvs_calculator_app)
