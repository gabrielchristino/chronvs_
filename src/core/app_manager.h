#pragma once

#include <stdbool.h>

#include "lvgl.h"

typedef struct {
    const char *id;
    const char *name;
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

const char *chronvs_app_active_id(void);
lv_obj_t *chronvs_app_content_layer(void);
