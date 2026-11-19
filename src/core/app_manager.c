#include "core/app_manager.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define CHRONVS_MAX_APPS 10
#define COLOR_VOID 0x050706

typedef struct {
    const chronvs_app_t *definition;
    lv_obj_t *root;
} app_entry_t;

static const char *TAG = "app_manager";
static app_entry_t apps[CHRONVS_MAX_APPS];
static size_t app_count;
static app_entry_t *active_app;
static app_entry_t *preview_app;
static lv_obj_t *content_layer;
static app_entry_t *settling_app, *settling_origin, *settling_destination;

static void settle_exec(void *object, int32_t y) {
    lv_obj_set_y(object, (lv_coord_t)y);
}

static void cancel_settle(void) {
    if (settling_app) lv_anim_del(settling_app->root, settle_exec);
    settling_app = settling_origin = settling_destination = NULL;
}

static void settle_ready(lv_anim_t *animation) {
    (void)animation;
    app_entry_t *destination = settling_destination;
    app_entry_t *origin = settling_origin;
    settling_app = settling_origin = settling_destination = NULL;
    if (!destination || active_app != origin) return;
    if (destination == active_app) chronvs_app_cancel_preview();
    else chronvs_app_open(destination->definition->id);
}

static app_entry_t *find_app(const char *id) {
    if (id == NULL) return NULL;
    for (size_t index = 0; index < app_count; ++index) {
        if (strcmp(apps[index].definition->id, id) == 0) return &apps[index];
    }
    return NULL;
}

void chronvs_app_manager_init(lv_obj_t *screen) {
    cancel_settle();
    app_count = 0;
    active_app = NULL;
    preview_app = NULL;

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
    if (next == active_app) {
        chronvs_app_cancel_preview();
        return true;
    }

    if (next->root == NULL) {
        next->root = next->definition->create(content_layer);
        if (next->root == NULL) {
            ESP_LOGE(TAG, "Could not create app: %s", next->definition->id);
            return false;
        }
    }

    cancel_settle();
    if (preview_app && preview_app != next)
        lv_obj_add_flag(preview_app->root, LV_OBJ_FLAG_HIDDEN);
    if (active_app != NULL) {
        if (active_app->definition->on_hide != NULL) {
            active_app->definition->on_hide();
        }
        lv_obj_add_flag(active_app->root, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(next->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(next->root, 0);
    lv_obj_set_y(next->root, 0);
    active_app = next;
    preview_app = NULL;
    if (next->definition->on_show != NULL) next->definition->on_show();
    ESP_LOGI(TAG, "Opened app: %s", next->definition->id);
    return true;
}

bool chronvs_app_preview(const char *id, lv_coord_t x) {
    app_entry_t *next = find_app(id);
    if (next == NULL || next == active_app) return false;
    cancel_settle();

    if (next->root == NULL) {
        next->root = next->definition->create(content_layer);
        if (next->root == NULL) return false;
    }

    if (preview_app != NULL && preview_app != next) {
        lv_obj_add_flag(preview_app->root, LV_OBJ_FLAG_HIDDEN);
    }
    preview_app = next;
    lv_obj_clear_flag(next->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(next->root, x);
    return true;
}

bool chronvs_app_preview_y(const char *id, lv_coord_t y) {
    app_entry_t *next = find_app(id);
    if (next == NULL || next == active_app) return false;
    cancel_settle();

    if (next->root == NULL) {
        next->root = next->definition->create(content_layer);
        if (next->root == NULL) return false;
    }

    if (preview_app != NULL && preview_app != next) {
        lv_obj_add_flag(preview_app->root, LV_OBJ_FLAG_HIDDEN);
    }
    preview_app = next;
    lv_obj_clear_flag(next->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(next->root, 0);
    lv_obj_set_y(next->root, y);
    return true;
}

void chronvs_app_set_active_x(lv_coord_t x) {
    cancel_settle();
    if (active_app != NULL) lv_obj_set_x(active_app->root, x);
}

void chronvs_app_set_active_y(lv_coord_t y) {
    cancel_settle();
    if (active_app != NULL) lv_obj_set_y(active_app->root, y);
}

void chronvs_app_cancel_preview(void) {
    cancel_settle();
    if (preview_app != NULL) {
        lv_obj_set_x(preview_app->root, 0);
        lv_obj_set_y(preview_app->root, 0);
        lv_obj_add_flag(preview_app->root, LV_OBJ_FLAG_HIDDEN);
        preview_app = NULL;
    }
    chronvs_app_set_active_x(0);
    chronvs_app_set_active_y(0);
}

bool chronvs_app_settle_y(const char *moving_id, lv_coord_t target_y,
                          const char *destination_id, uint32_t duration_ms,
                          lv_anim_path_cb_t path) {
    app_entry_t *moving = find_app(moving_id);
    app_entry_t *destination = find_app(destination_id);
    if (!moving || !moving->root || !destination ||
        (moving != active_app && moving != preview_app) ||
        (destination != active_app && destination != preview_app)) return false;
#ifdef CHRONVS_PANEL_SNAP_REFERENCE
    (void)target_y; (void)duration_ms; (void)path;
    if (destination == active_app) chronvs_app_cancel_preview();
    else chronvs_app_open(destination_id);
    return true;
#endif
    cancel_settle();
    lv_obj_update_layout(moving->root);
    settling_app = moving;
    settling_origin = active_app;
    settling_destination = destination;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, moving->root);
    lv_anim_set_exec_cb(&animation, settle_exec);
    lv_anim_set_values(&animation, lv_obj_get_y(moving->root), target_y);
    lv_anim_set_time(&animation, duration_ms);
    lv_anim_set_path_cb(&animation, path ? path : lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&animation, settle_ready);
    if (!lv_anim_start(&animation)) {
        settle_exec(moving->root, target_y);
        settle_ready(&animation);
    }
    return true;
}

bool chronvs_app_is_settling(void) { return settling_app != NULL; }

const char *chronvs_app_active_id(void) {
    return active_app != NULL ? active_app->definition->id : NULL;
}

size_t chronvs_app_count(void) {
    return app_count;
}

const chronvs_app_t *chronvs_app_at(size_t index) {
    return index < app_count ? apps[index].definition : NULL;
}

lv_obj_t *chronvs_app_content_layer(void) {
    return content_layer;
}
