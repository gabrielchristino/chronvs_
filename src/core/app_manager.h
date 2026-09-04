#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

typedef void (*chronvs_app_icon_create_cb_t)(lv_obj_t *parent);

typedef struct {
    const char *id;
    const char *name;
    chronvs_app_icon_create_cb_t create_icon;
    bool launcher_visible;
    lv_obj_t *(*create)(lv_obj_t *parent);
    void (*on_show)(void);
    void (*on_hide)(void);
} chronvs_app_t;

/* Creates the content layer that sits below global system UI. */
void chronvs_app_manager_init(lv_obj_t *screen);

/* Registers an app definition. IDs must be unique and remain valid forever. */
bool chronvs_app_register(const chronvs_app_t *app);

/* Lazily creates and displays an app by ID. */
bool chronvs_app_open(const char *id);

/* Shows an app temporarily at an x offset during a drag transition. */
bool chronvs_app_preview(const char *id, lv_coord_t x);
void chronvs_app_set_active_x(lv_coord_t x);
void chronvs_app_cancel_preview(void);

size_t chronvs_app_count(void);
const chronvs_app_t *chronvs_app_at(size_t index);

const char *chronvs_app_active_id(void);
lv_obj_t *chronvs_app_content_layer(void);
