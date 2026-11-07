#include "ui/power_menu.h"
#include "platform/board.h"
#include "ui/Relogio_widgets.h"
#include "ui/app_input.h"
#include "ui/system_ui.h"
#include "core/app_manager.h"

enum { NONE, CANCEL, POWER_OFF, RESTART };
static lv_obj_t *overlay;
static int pending_action;

static void cancel(void) { pending_action = CANCEL; }
static chronvs_ui_app_input_t input = {.back = cancel};

static void choose(lv_event_t *event) {
    pending_action = (intptr_t)lv_event_get_user_data(event);
}

void chronvs_power_menu_poll(void) {
    /* Execute outside input dispatch: callbacks only record the selection. */
    int action = pending_action;
    pending_action = NONE;
    if (action != NONE && overlay) {
        lv_obj_del(overlay);
        overlay = NULL;
        if (action == POWER_OFF || action == RESTART) {
            /* Let the current app persist pending edits through on_hide. */
            chronvs_app_open("watch");
            if (action == POWER_OFF) chronvs_board_power_off();
            else chronvs_board_restart();
        }
    }
    if (!chronvs_board_take_power_menu_request()) return;
    chronvs_system_ui_notify_activity();
    for (lv_indev_t *device = lv_indev_get_next(NULL); device;
         device = lv_indev_get_next(device)) {
        /* LVGL 8: an idle device must accept the very first menu touch. */
        if (device->proc.state == LV_INDEV_STATE_PR) lv_indev_wait_release(device);
    }
    if (overlay) {
        lv_obj_move_foreground(overlay);
        return;
    }
    overlay = lv_obj_create(lv_layer_top());
    chronvs_Relogio_surface(overlay);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_t *title = chronvs_Relogio_label(overlay, "Energia", 64,
                                            &lv_font_montserrat_24);
    lv_obj_set_style_text_color(title, lv_color_hex(CHRONVS_UI_ACCENT), 0);
    const char *symbols[] = {LV_SYMBOL_REFRESH, LV_SYMBOL_POWER, LV_SYMBOL_CLOSE};
    const int actions[] = {RESTART, POWER_OFF, CANCEL};
    const lv_point_t positions[] = {{-53, 135}, {53, 135}, {0, 227}};
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_t *button = chronvs_Relogio_circle(overlay, symbols[i], i + 2,
                                                  100, choose, actions[i]);
        if (actions[i] == CANCEL) chronvs_ui_style_control(button, true);
        /* Explicit larger-target exception for the three power actions. */
        lv_obj_set_size(button, 90, 90);
        lv_obj_align(button, LV_ALIGN_TOP_MID, positions[i].x, positions[i].y);
        /* Keep the contact when a back swipe crosses adjacent circles. */
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_t *symbol = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_font(symbol, &lv_font_montserrat_48, 0);
        lv_obj_center(symbol);
    }
    chronvs_ui_app_input_bind(overlay, &input);
    lv_obj_update_layout(overlay);
}
