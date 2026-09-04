#pragma once

#include <stdbool.h>

#include "core/app_manager.h"

// Registers every application shipped in the firmware.
bool chronvs_apps_register_all(void);

void chronvs_apps_add(const chronvs_app_t *app);

#define CHRONVS_REGISTER_APP(symbol) \
    static void chronvs_register_##symbol(void) __attribute__((constructor)); \
    static void chronvs_register_##symbol(void) { chronvs_apps_add(&(symbol)); }
