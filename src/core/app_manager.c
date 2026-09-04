#include "core/app_manager.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define CHRONVS_MAX_APPS 8
#define COLOR_VOID 0x050706

typedef struct {
    const chronvs_app_t *definition;
    lv_obj_t *root;
} app_entry_t;

static const char *TAG = "app_manager";
static app_entry_t apps[CHRONVS_MAX_APPS];
static size_t app_count;
static app_entry_t *active_app;
static lv_obj_t *content_layer;

static app_entry_t *find_app(const char *id) {
    if (id == NULL) return NULL;
    for (size_t index = 0; index < app_count; ++index) {
        if (strcmp(apps[index].definition->id, id) == 0) return &apps[index];
    }
    return NULL;
}

void chronvs_app_manager_init(lv_obj_t *screen) {
    app_count = 0;
    active_app = NULL;

    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_VOID), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    content_layer = lv_obj_create(screen);
    lv_obj_remove_style_all(content_layer);
    lv_obj_set_size(content_layer, lv_pct(100), lv_pct(100));
    lv_obj_center(content_layer);
    lv_obj_clear_flag(content_layer, LV_OBJ_FLAG_SCROLLABLE);
}

bool chronvs_app_register(const chronvs_app_t *app) {
    if (content_layer == NULL || app == NULL || app->id == NULL ||
        app->create == NULL || find_app(app->id) != NULL ||
        app_count >= CHRONVS_MAX_APPS) {
        return false;
    }

    apps[app_count++] = (app_entry_t){.definition = app, .root = NULL};
    ESP_LOGI(TAG, "Registered app: %s (%s)", app->id,
             app->name != NULL ? app->name : app->id);
    return true;
}

bool chronvs_app_open(const char *id) {
    app_entry_t *next = find_app(id);
    if (next == NULL) {
        ESP_LOGW(TAG, "Unknown app: %s", id != NULL ? id : "(null)");
        return false;
    }
    if (next == active_app) return true;

    if (next->root == NULL) {
        next->root = next->definition->create(content_layer);
        if (next->root == NULL) {
            ESP_LOGE(TAG, "Could not create app: %s", next->definition->id);
            return false;
        }
    }

    if (active_app != NULL) {
        if (active_app->definition->on_hide != NULL) {
            active_app->definition->on_hide();
        }
        lv_obj_add_flag(active_app->root, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(next->root, LV_OBJ_FLAG_HIDDEN);
    active_app = next;
    if (next->definition->on_show != NULL) next->definition->on_show();
    ESP_LOGI(TAG, "Opened app: %s", next->definition->id);
    return true;
}

const char *chronvs_app_active_id(void) {
    return active_app != NULL ? active_app->definition->id : NULL;
}

lv_obj_t *chronvs_app_content_layer(void) {
    return content_layer;
}
