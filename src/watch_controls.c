#include "watch_controls.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "Display_SPD2010.h"

#define COLOR_PANEL       0x26302B
#define COLOR_PANEL_EDGE  0x748173
#define COLOR_TEXT        0xF2F2E9
#define COLOR_TEXT_DIM    0xB7C0B5
#define COLOR_ACCENT      0xF2B84B
#define COLOR_SLIDER      0x91A18B

#define DEFAULT_BRIGHTNESS 70
#define DIM_BRIGHTNESS 12
#define POWER_TIMER_PERIOD_MS 500
#define CLOCK_REDRAW_PERIOD_MS 1000

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

static const char *TAG = "controls";
static lv_obj_t *clock_surface;
static lv_obj_t *settings_panel;
static lv_obj_t *brightness_label;
static lv_obj_t *profile_label;
static lv_timer_t *clock_animation_timer;
static nvs_handle_t settings_storage;
static uint8_t selected_brightness = DEFAULT_BRIGHTNESS;
static uint8_t selected_profile;
static uint8_t output_brightness = UINT8_MAX;
static display_state_t display_state = DISPLAY_ACTIVE;
static uint32_t last_activity_tick;
static bool wake_only_click;
static bool long_press_seen;
static bool touch_contact_active;

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

static void set_display_state(display_state_t state) {
    if (state == display_state && output_brightness != UINT8_MAX) return;
    display_state = state;

    if (state == DISPLAY_ACTIVE) {
        set_output_brightness(selected_brightness);
        lv_timer_set_period(clock_animation_timer, CLOCK_REDRAW_PERIOD_MS);
        lv_timer_resume(clock_animation_timer);
        lv_obj_invalidate(clock_surface);
    }
    else if (state == DISPLAY_DIMMED) {
        const uint8_t dimmed = selected_brightness < DIM_BRIGHTNESS
                                  ? selected_brightness : DIM_BRIGHTNESS;
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
}

static void update_brightness_label(void) {
    if (brightness_label == NULL) return;
    lv_label_set_text_fmt(brightness_label, "BRILHO  %u%%", selected_brightness);
}

static void brightness_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *slider = lv_event_get_target(event);

    if (code == LV_EVENT_VALUE_CHANGED) {
        selected_brightness = (uint8_t)lv_slider_get_value(slider);
        display_state = DISPLAY_ACTIVE;
        set_output_brightness(selected_brightness);
        update_brightness_label();
        mark_activity();
    }
    else if (code == LV_EVENT_RELEASED) {
        store_u8("brightness", selected_brightness);
    }
}

static void profile_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    selected_profile = (uint8_t)((selected_profile + 1) %
        (sizeof(power_profiles) / sizeof(power_profiles[0])));
    lv_label_set_text(profile_label, power_profiles[selected_profile].label);
    store_u8("power", selected_profile);
    mark_activity();
}

static void close_settings_event(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *panel = settings_panel;
    settings_panel = NULL;
    brightness_label = NULL;
    profile_label = NULL;
    lv_obj_del_async(panel);
    mark_activity();
    set_display_state(DISPLAY_ACTIVE);
}

static void style_button(lv_obj_t *button) {
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL_EDGE), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}

static void open_settings(void) {
    if (settings_panel != NULL) return;

    lv_obj_t *screen = lv_scr_act();
    settings_panel = lv_obj_create(screen);
    lv_obj_set_size(settings_panel, 300, 230);
    lv_obj_center(settings_panel);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(settings_panel, lv_color_hex(COLOR_PANEL_EDGE), 0);
    lv_obj_set_style_border_width(settings_panel, 2, 0);
    lv_obj_set_style_radius(settings_panel, 34, 0);
    lv_obj_set_style_pad_all(settings_panel, 0, 0);

    lv_obj_t *title = lv_label_create(settings_panel);
    lv_label_set_text(title, "AJUSTES");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    brightness_label = lv_label_create(settings_panel);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 57);
    update_brightness_label();

    lv_obj_t *slider = lv_slider_create(settings_panel);
    lv_obj_set_size(slider, 220, 18);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 82);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, selected_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_SLIDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_event, LV_EVENT_ALL, NULL);

    lv_obj_t *profile_button = lv_btn_create(settings_panel);
    lv_obj_set_size(profile_button, 190, 42);
    lv_obj_align(profile_button, LV_ALIGN_TOP_MID, 0, 118);
    style_button(profile_button);
    lv_obj_add_event_cb(profile_button, profile_event, LV_EVENT_CLICKED, NULL);

    profile_label = lv_label_create(profile_button);
    lv_label_set_text(profile_label, power_profiles[selected_profile].label);
    lv_obj_set_style_text_font(profile_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(profile_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(profile_label);

    lv_obj_t *close_button = lv_btn_create(settings_panel);
    lv_obj_set_size(close_button, 100, 38);
    lv_obj_align(close_button, LV_ALIGN_BOTTOM_MID, 0, -15);
    style_button(close_button);
    lv_obj_add_event_cb(close_button, close_settings_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "FECHAR");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(close_label);

    lv_obj_move_foreground(settings_panel);
    mark_activity();
    set_display_state(DISPLAY_ACTIVE);
}

static void clock_touch_event(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        if (touch_contact_active) return;
        touch_contact_active = true;
        wake_only_click = display_state != DISPLAY_ACTIVE;
        long_press_seen = false;
        if (wake_only_click) {
            mark_activity();
            set_display_state(DISPLAY_ACTIVE);
        }
    }
    else if (code == LV_EVENT_LONG_PRESSED) {
        /* A contact used to wake a dimmed/off display must be released before
         * it can perform any gesture.  This also prevents a stale touch report
         * from waking the watch and immediately opening the settings panel. */
        if (wake_only_click) return;
        long_press_seen = true;
        open_settings();
    }
    else if (code == LV_EVENT_CLICKED) {
        mark_activity();
        set_display_state(DISPLAY_ACTIVE);
        if (!wake_only_click && !long_press_seen && settings_panel == NULL) {
            static const uint8_t levels[] = {40, 70, 100};
            size_t next = 0;
            while (next < sizeof(levels) && levels[next] <= selected_brightness) ++next;
            if (next == sizeof(levels)) next = 0;
            selected_brightness = levels[next];
            set_output_brightness(selected_brightness);
            store_u8("brightness", selected_brightness);
            ESP_LOGI(TAG, "Brightness set to %u%%", selected_brightness);
        }
        wake_only_click = false;
        long_press_seen = false;
    }
    else if (code == LV_EVENT_RELEASED) {
        touch_contact_active = false;
    }
}

static void power_timer_event(lv_timer_t *timer) {
    (void)timer;
    if (settings_panel != NULL) return;

    const uint32_t inactive_ms = lv_tick_elaps(last_activity_tick);
    const power_profile_t *profile = &power_profiles[selected_profile];
    if (inactive_ms >= profile->off_after_ms) {
        set_display_state(DISPLAY_OFF);
    }
    else if (inactive_ms >= profile->dim_after_ms) {
        set_display_state(DISPLAY_DIMMED);
    }
}

void chronvs_watch_controls_init(lv_obj_t *touch_surface,
                                 lv_timer_t *animation_timer) {
    clock_surface = touch_surface;
    clock_animation_timer = animation_timer;
    load_settings();
    last_activity_tick = lv_tick_get();

    lv_obj_add_flag(clock_surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_surface, clock_touch_event, LV_EVENT_ALL, NULL);
    lv_timer_create(power_timer_event, POWER_TIMER_PERIOD_MS, NULL);
    set_display_state(DISPLAY_ACTIVE);

    ESP_LOGI(TAG, "Touch controls ready: brightness %u%%, %s",
             selected_brightness, power_profiles[selected_profile].label);
}

bool chronvs_watch_display_is_off(void) {
    return display_state == DISPLAY_OFF;
}
