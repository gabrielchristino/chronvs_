#include "apps/app_catalog.h"

#include <stddef.h>

#include "apps/watch_app.h"
#include "core/app_manager.h"

bool chronvs_apps_register_all(void) {
    const chronvs_app_t *const apps[] = {
        &chronvs_watch_app,
    };

    for (size_t index = 0; index < sizeof(apps) / sizeof(apps[0]); ++index) {
        if (!chronvs_app_register(apps[index])) return false;
    }
    return true;
}
