#include "apps/app_catalog.h"

#include <stddef.h>

#include "core/app_manager.h"

static const chronvs_app_t *discovered_apps[8];
static size_t discovered_count;

void chronvs_apps_add(const chronvs_app_t *app) {
    if (app != NULL && discovered_count < sizeof(discovered_apps) / sizeof(discovered_apps[0])) {
        discovered_apps[discovered_count++] = app;
    }
}

bool chronvs_apps_register_all(void) {
    for (size_t index = 0; index < discovered_count; ++index) {
        if (!chronvs_app_register(discovered_apps[index])) return false;
    }
    return true;
}
