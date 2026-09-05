#include "ui/system_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "Display_SPD2010.h"
#include "core/app_manager.h"

#define COLOR_PANEL       0x26302B
#define COLOR_PANEL_EDGE  0x748173
#define COLOR_TEXT        0xF2F2E9
#define COLOR_TEXT_DIM    0xB7C0B5
#define COLOR_ACCENT      0xF2B84B

#define DEFAULT_BRIGHTNESS 70
#define DIM_BRIGHTNESS 12
#define ECO_MAX_BRIGHTNESS 35
#define ECO_DIM_BRIGHTNESS 5
#define ECO_DIM_AFTER_MS 5000
#define ECO_OFF_AFTER_MS 15000
#define POWER_TIMER_PERIOD_MS 500
#define CLOCK_REDRAW_PERIOD_MS 1000
#define MENU_EDGE_START_Y 90
#define MENU_DRAG_SLOP 5
#define MENU_DRAG_FRAME_MS 20
#define MENU_ANIMATION_MS 220
#define MENU_OPEN_COMMIT_DISTANCE 120
#define MENU_OPEN_FOLLOW_GAIN 2
#define APP_SWIPE_COMMIT_DISTANCE 120
#define APP_SWIPE_FOLLOW_GAIN 2

typedef enum {
    DISPLAY_ACTIVE,
    DISPLAY_DIMMED,
    DISPLAY_OFF,
} display_state_t;

typedef struct {
    uint32_t dim_after_ms;
    uint32_t off_after_ms;
    const char *label;
} power_profile_t;

static const power_profile_t power_profiles[] = {
    {15000, 45000, "AUTO 15s / 45s"},
    {30000, 120000, "AUTO 30s / 2min"},
    {UINT32_MAX, UINT32_MAX, "SEMPRE LIGADA"},
};

static const char *power_profile_short_labels[] = {"15s", "30s", "ON"};

static const char *TAG = "controls";
static lv_obj_t *clock_surface;
static lv_obj_t *settings_panel;
static lv_obj_t *brightness_label;
static lv_obj_t *profile_label;
static lv_obj_t *brightness_arc;
static lv_obj_t *profile_button;
static lv_obj_t *battery_button;
static lv_obj_t *app_launcher_button;
static lv_obj_t *battery_label;
static lv_obj_t *battery_eco_label;
static lv_timer_t *clock_animation_timer;
static nvs_handle_t settings_storage;
static uint8_t selected_brightness = DEFAULT_BRIGHTNESS;
static uint8_t selected_profile;
static bool eco_enabled;
static uint8_t output_brightness = UINT8_MAX;
static display_state_t display_state = DISPLAY_ACTIVE;
static uint32_t last_activity_tick;
static bool wake_only_contact;
static bool touch_contact_active;
static bool menu_open;
static bool menu_dragging;
static bool menu_suppress_click;
static bool clock_swipe_candidate;
static bool app_swipe_dragging;
static int16_t gesture_start_x;
static int16_t gesture_start_y;
static int16_t menu_gesture_start_y;
static uint32_t last_menu_drag_tick;
static uint32_t last_app_drag_tick;

static void animate_menu(bool open);

static void mark_activity(void) {
    last_activity_tick = lv_tick_get();
    lv_disp_trig_activity(NULL);
}

static void set_output_brightness(uint8_t brightness) {
    if (brightness == output_brightness) return;
    output_brightness = brightness;
    LCD_Backlight = brightness;
    Set_Backlight(brightness);
}

static uint8_t active_output_brightness(void) {
    return eco_enabled && selected_brightness > ECO_MAX_BRIGHTNESS
               ? ECO_MAX_BRIGHTNESS : selected_brightness;
}

static void set_display_state(display_state_t state) {
    if (state == display_state && output_brightness != UINT8_MAX) return;
    display_state = state;

    if (state == DISPLAY_ACTIVE) {
        set_output_brightness(active_output_brightness());
        lv_timer_set_period(clock_animation_timer, CLOCK_REDRAW_PERIOD_MS);
        lv_timer_resume(clock_animation_timer);
        lv_obj_invalidate(clock_surface);
    }
    else if (state == DISPLAY_DIMMED) {
        const uint8_t dim_limit = eco_enabled ? ECO_DIM_BRIGHTNESS : DIM_BRIGHTNESS;
        const uint8_t active = active_output_brightness();
        const uint8_t dimmed = active < dim_limit ? active : dim_limit;
        set_output_brightness(dimmed);
        lv_timer_set_period(clock_animation_timer, CLOCK_REDRAW_PERIOD_MS);
        lv_timer_resume(clock_animation_timer);
    }
    else { /* DISPLAY_OFF */
        set_output_brightness(0);
        lv_timer_pause(clock_animation_timer);
    }

    ESP_LOGI(TAG, "Display %s", state == DISPLAY_ACTIVE ? "active" :
             state == DISPLAY_DIMMED ? "dimmed" : "off");
}

static void store_u8(const char *key, uint8_t value) {
    if (settings_storage == 0) return;
    esp_err_t result = nvs_set_u8(settings_storage, key, value);
    if (result == ESP_OK) result = nvs_commit(settings_storage);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not save %s: %s", key, esp_err_to_name(result));
    }
}

static void load_settings(void) {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() == ESP_OK) result = nvs_flash_init();
    }
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Settings storage unavailable: %s", esp_err_to_name(result));
        return;
    }

    result = nvs_open("chronvs", NVS_READWRITE, &settings_storage);
    if (result != ESP_OK) {
        settings_storage = 0;
        ESP_LOGW(TAG, "Could not open settings: %s", esp_err_to_name(result));
        return;
    }

    uint8_t value;
    if (nvs_get_u8(settings_storage, "brightness", &value) == ESP_OK &&
        value >= 10 && value <= 100) {
        selected_brightness = value;
    }
    if (nvs_get_u8(settings_storage, "power", &value) == ESP_OK &&
        value < sizeof(power_profiles) / sizeof(power_profiles[0])) {
        selected_profile = value;
    }
    if (nvs_get_u8(settings_storage, "eco", &value) == ESP_OK) {
        eco_enabled = value != 0;
    }
}

static void update_brightness_label(void) {
    if (brightness_label == NULL) return;
    lv_label_set_text_fmt(brightness_label, "BRILHO  %u%%",
                          active_output_brightness());
}

static void update_brightness_control(void) {
    if (brightness_arc == NULL) return;
    lv_arc_set_range(brightness_arc, 10, 100);
    lv_arc_set_value(brightness_arc, active_output_brightness());
    update_brightness_label();
}

static void brightness_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_VALUE_CHANGED) {
        selected_brightness = (uint8_t)lv_arc_get_value(brightness_arc);
        display_state = DISPLAY_ACTIVE;
        set_output_brightness(active_output_brightness());
        if (eco_enabled && selected_brightness > ECO_MAX_BRIGHTNESS) {
            lv_arc_set_value(brightness_arc, ECO_MAX_BRIGHTNESS);
        }
        update_brightness_label();
        mark_activity();
    }
    else if (code == LV_EVENT_RELEASED) {
        store_u8("brightness", selected_brightness);
    }
}

static void update_battery_eco_button(void) {
    if (battery_button == NULL || battery_eco_label == NULL) return;
    lv_obj_set_style_bg_color(battery_button,
        lv_color_hex(eco_enabled ? COLOR_PANEL_EDGE : COLOR_PANEL), 0);
    lv_obj_set_style_border_color(battery_button,
        lv_color_hex(eco_enabled ? COLOR_ACCENT : COLOR_PANEL_EDGE), 0);
    lv_label_set_text(battery_eco_label, eco_enabled ? "ECO" : "");
}

static void eco_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || menu_suppress_click) return;
    eco_enabled = !eco_enabled;
    display_state = DISPLAY_ACTIVE;
    update_brightness_control();
    set_output_brightness(active_output_brightness());
    update_battery_eco_button();
    store_u8("eco", eco_enabled ? 1 : 0);
    mark_activity();
    ESP_LOGI(TAG, "Economy mode %s", eco_enabled ? "enabled" : "disabled");
}

static void profile_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || menu_suppress_click) return;
    selected_profile = (uint8_t)((selected_profile + 1) %
        (sizeof(power_profiles) / sizeof(power_profiles[0])));
    lv_label_set_text(profile_label, power_profile_short_labels[selected_profile]);
    store_u8("power", selected_profile);
    mark_activity();
}

static void app_launcher_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || menu_suppress_click) return;
    if (chronvs_app_open("apps")) animate_menu(false);
}

static void style_button(lv_obj_t *button) {
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL_EDGE), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

static lv_obj_t *create_round_slot(lv_obj_t *parent, int16_t x, int16_t y,
                                   bool enabled) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 70, 70);
    lv_obj_align(button, LV_ALIGN_CENTER, x, y);
    style_button(button);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);

    if (!enabled) {
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(COLOR_PANEL_EDGE), 0);
        lv_obj_set_style_border_width(button, 2, 0);
    }
    return button;
}

static int16_t menu_height(void) {
    return (int16_t)lv_obj_get_height(settings_panel);
}

static int16_t clamp_menu_y(int32_t y) {
    const int16_t height = menu_height();
    if (y < -height) return -height;
    if (y > 0) return 0;
    return (int16_t)y;
}

static void menu_set_y(void *object, int32_t y) {
    lv_obj_set_y((lv_obj_t *)object, (lv_coord_t)y);
}

static void menu_animation_ready(lv_anim_t *animation) {
    lv_obj_t *panel = (lv_obj_t *)animation->var;
    if (menu_open) {
        lv_obj_set_y(panel, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_set_y(panel, -menu_height());
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(clock_surface);
    }
}

static void set_menu_interactive(bool interactive) {
    lv_obj_t *objects[] = {
        settings_panel, brightness_arc, profile_button, battery_button, app_launcher_button,
    };
    for (size_t index = 0; index < sizeof(objects) / sizeof(objects[0]); ++index) {
        if (interactive) lv_obj_add_flag(objects[index], LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_clear_flag(objects[index], LV_OBJ_FLAG_CLICKABLE);
    }
}

static void animate_menu(bool open) {
    const int16_t target_y = open ? 0 : -menu_height();
    menu_open = open;
    menu_dragging = false;

    if (open) {
        lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_panel);
    }
    set_menu_interactive(open);

    lv_anim_del(settings_panel, NULL);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, settings_panel);
    lv_anim_set_exec_cb(&animation, menu_set_y);
    lv_anim_set_values(&animation, lv_obj_get_y(settings_panel), target_y);
    lv_anim_set_time(&animation, MENU_ANIMATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&animation, menu_animation_ready);
    lv_anim_start(&animation);
    mark_activity();
}

static void begin_menu_drag(bool opening) {
    lv_anim_del(settings_panel, NULL);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings_panel);
    if (opening) set_menu_interactive(false);
    last_menu_drag_tick = 0;
}

static void place_menu_at(int16_t y) {
    const uint32_t now = lv_tick_get();
    if (last_menu_drag_tick != 0 &&
        lv_tick_elaps(last_menu_drag_tick) < MENU_DRAG_FRAME_MS) {
        return;
    }
    last_menu_drag_tick = now;
    lv_obj_set_y(settings_panel, clamp_menu_y(y));
}

static void place_app_preview_above(lv_coord_t y) {
    const uint32_t now = lv_tick_get();
    if (last_app_drag_tick != 0 &&
        lv_tick_elaps(last_app_drag_tick) < MENU_DRAG_FRAME_MS) {
        return;
    }
    last_app_drag_tick = now;
    chronvs_app_preview_y("apps", y);
}

static void menu_touch_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        menu_gesture_start_y = point.y;
        menu_dragging = false;
        menu_suppress_click = false;
    }
    else if (code == LV_EVENT_PRESSING) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        const int16_t distance = point.y - menu_gesture_start_y;
        if (distance < -MENU_DRAG_SLOP) {
            if (!menu_dragging) begin_menu_drag(false);
            menu_dragging = true;
            menu_suppress_click = true;
            place_menu_at(distance);
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (menu_dragging) {
            animate_menu(false);
        }
        else {
            mark_activity();
        }
    }
}

static void create_quick_settings(void) {
    lv_obj_t *screen = lv_scr_act();
    settings_panel = lv_obj_create(screen);
    const lv_coord_t width = lv_disp_get_hor_res(NULL);
    const lv_coord_t height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(settings_panel, width, height);
    lv_obj_set_pos(settings_panel, 0, -height);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_panel, 0, 0);
    lv_obj_set_style_radius(settings_panel, 0, 0);
    lv_obj_set_style_pad_all(settings_panel, 0, 0);
    lv_obj_add_event_cb(settings_panel, menu_touch_event, LV_EVENT_ALL, NULL);

    brightness_arc = lv_arc_create(settings_panel);
    lv_obj_set_size(brightness_arc, width - 24, height - 24);
    lv_obj_center(brightness_arc);
    lv_obj_add_flag(brightness_arc, LV_OBJ_FLAG_ADV_HITTEST);
    
    lv_arc_set_range(brightness_arc, 10, 100);
    lv_arc_set_bg_angles(brightness_arc, 135, 45);
    lv_arc_set_value(brightness_arc, selected_brightness);
    lv_obj_set_style_arc_color(brightness_arc, lv_color_hex(COLOR_PANEL_EDGE), LV_PART_MAIN);
    lv_obj_set_style_arc_width(brightness_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(brightness_arc, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(brightness_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_arc, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_arc, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_arc, brightness_event, LV_EVENT_ALL, NULL);

    brightness_label = lv_label_create(settings_panel);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 48);
    update_brightness_label();

    profile_button = create_round_slot(settings_panel, -45, -75, true);
    lv_obj_add_flag(profile_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(profile_button, profile_event, LV_EVENT_CLICKED, NULL);

    profile_label = lv_label_create(profile_button);
    lv_label_set_text(profile_label, power_profile_short_labels[selected_profile]);
    lv_obj_set_style_text_font(profile_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(profile_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(profile_label);

    battery_button = create_round_slot(settings_panel, 45, -75, true);
    lv_obj_add_flag(battery_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(battery_button, eco_event, LV_EVENT_CLICKED, NULL);

    battery_label = lv_label_create(battery_button);
    lv_label_set_text(battery_label, "--%");
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(battery_label, LV_ALIGN_CENTER, 0, -8);

    battery_eco_label = lv_label_create(battery_button);
    lv_obj_set_style_text_font(battery_eco_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(battery_eco_label, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(battery_eco_label, LV_ALIGN_CENTER, 0, 16);
    update_battery_eco_button();
    update_brightness_control();

    app_launcher_button = create_round_slot(settings_panel, 0, 5, true);
    lv_obj_add_flag(app_launcher_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(app_launcher_button, app_launcher_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *app_launcher_label = lv_label_create(app_launcher_button);
    lv_label_set_text(app_launcher_label, "APPS");
    lv_obj_set_style_text_font(app_launcher_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(app_launcher_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(app_launcher_label);

    static const int16_t placeholder_positions[][2] = {
        {-78, 5}, {78, 5},
        {-45, 85}, {45, 85},
    };
    for (size_t index = 0;
         index < sizeof(placeholder_positions) / sizeof(placeholder_positions[0]);
         ++index) {
        create_round_slot(settings_panel,
                          placeholder_positions[index][0],
                          placeholder_positions[index][1], false);
    }

    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
}

static void clock_touch_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        if (touch_contact_active) return;
        touch_contact_active = true;
        wake_only_contact = display_state != DISPLAY_ACTIVE;
        menu_dragging = false;
        lv_indev_get_point(lv_indev_get_act(), &point);
        gesture_start_x = point.x;
        gesture_start_y = point.y;
        clock_swipe_candidate = !wake_only_contact;
        if (wake_only_contact) {
            mark_activity();
            set_display_state(DISPLAY_ACTIVE);
        }
    }
    else if (code == LV_EVENT_PRESSING && app_swipe_dragging) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        int16_t distance = gesture_start_y - point.y;
        if (distance < 0) distance = 0;
        const int16_t height = lv_disp_get_ver_res(NULL);
        if (distance > height / APP_SWIPE_FOLLOW_GAIN) {
            distance = height / APP_SWIPE_FOLLOW_GAIN;
        }
        place_app_preview_above(height - distance * APP_SWIPE_FOLLOW_GAIN);
        mark_activity();
    }
    else if (code == LV_EVENT_PRESSING && clock_swipe_candidate) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        const int16_t dx = point.x - gesture_start_x;
        const int16_t dy = point.y - gesture_start_y;
        if (dy < -MENU_DRAG_SLOP && -dy > (dx < 0 ? -dx : dx) + 12) {
            const int16_t height = lv_disp_get_ver_res(NULL);
            int16_t distance = -dy;
            if (distance > height / APP_SWIPE_FOLLOW_GAIN) {
                distance = height / APP_SWIPE_FOLLOW_GAIN;
            }
            last_app_drag_tick = 0;
            app_swipe_dragging = chronvs_app_preview_y(
                "apps", height - distance * APP_SWIPE_FOLLOW_GAIN);
            mark_activity();
        }
        else if (gesture_start_y <= MENU_EDGE_START_Y &&
                 dy > MENU_DRAG_SLOP && dx < dy + 30 && dx > -dy - 30) {
            if (!menu_dragging) begin_menu_drag(true);
            menu_dragging = true;
            menu_open = true;
            place_menu_at(point.y * MENU_OPEN_FOLLOW_GAIN - menu_height());
            mark_activity();
        }
        else if (dx > 45 || dx < -45) {
            clock_swipe_candidate = false;
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (app_swipe_dragging) {
            lv_indev_get_point(lv_indev_get_act(), &point);
            if (gesture_start_y - point.y >= APP_SWIPE_COMMIT_DISTANCE) {
                chronvs_app_open("apps");
            }
            else {
                chronvs_app_cancel_preview();
            }
            app_swipe_dragging = false;
        }
        else if (menu_dragging) {
            lv_indev_get_point(lv_indev_get_act(), &point);
            lv_obj_set_y(settings_panel,
                         clamp_menu_y(point.y * MENU_OPEN_FOLLOW_GAIN -
                                      menu_height()));
            if (point.y >= MENU_OPEN_COMMIT_DISTANCE) {
                animate_menu(true);
            } else {
                animate_menu(false);
            }
        }
        else if (!wake_only_contact) {
            mark_activity();
        }
        touch_contact_active = false;
        wake_only_contact = false;
        clock_swipe_candidate = false;
    }
}

static void power_timer_event(lv_timer_t *timer) {
    (void)timer;
    if (menu_open || menu_dragging) return;

    const uint32_t inactive_ms = lv_tick_elaps(last_activity_tick);
    const power_profile_t *profile = &power_profiles[selected_profile];
    const uint32_t dim_after_ms = eco_enabled ? ECO_DIM_AFTER_MS : profile->dim_after_ms;
    const uint32_t off_after_ms = eco_enabled ? ECO_OFF_AFTER_MS : profile->off_after_ms;
    if (inactive_ms >= off_after_ms) {
        set_display_state(DISPLAY_OFF);
    }
    else if (inactive_ms >= dim_after_ms) {
        set_display_state(DISPLAY_DIMMED);
    }
}

void chronvs_system_ui_init(lv_obj_t *touch_surface,
                            lv_timer_t *animation_timer) {
    clock_surface = touch_surface;
    clock_animation_timer = animation_timer;
    load_settings();
    last_activity_tick = lv_tick_get();
    create_quick_settings();

    lv_obj_add_flag(clock_surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_surface, clock_touch_event, LV_EVENT_ALL, NULL);
    lv_timer_create(power_timer_event, POWER_TIMER_PERIOD_MS, NULL);
    set_display_state(DISPLAY_ACTIVE);

    ESP_LOGI(TAG, "Touch controls ready: brightness %u%%, %s",
             selected_brightness, power_profiles[selected_profile].label);
}

bool chronvs_system_ui_display_is_off(void) {
    return display_state == DISPLAY_OFF;
}

void chronvs_system_ui_notify_activity(void) {
    mark_activity();
    if (display_state != DISPLAY_ACTIVE) set_display_state(DISPLAY_ACTIVE);
}

void chronvs_system_ui_set_battery(uint8_t percent, float voltage) {
    if (battery_label == NULL) return;
    if (percent > 100) percent = 100;
    lv_label_set_text_fmt(battery_label, "%u%%", percent);
    lv_obj_set_style_text_color(battery_label,
        lv_color_hex(percent <= 15 ? 0xE47470 :
                     percent <= 35 ? COLOR_ACCENT : COLOR_TEXT), 0);
    ESP_LOGI(TAG, "Battery %.2f V (%u%%)", voltage, percent);
}
