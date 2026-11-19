/* Real recorder, host directory and clock adapter for the LVGL integration. */
#include <direct.h>
#include <process.h>
#include <stdio.h>
#include <time.h>
static char host_directory[64];
static struct tm *host_gmtime_r(const time_t *value, struct tm *out) {
    return gmtime_s(out, value) == 0 ? out : NULL;
}
#define gmtime_r host_gmtime_r
#define fsync _commit
#define CHRONVS_RUN_DIRECTORY host_directory
#include "services/run_service.c"
bool chronvs_sd_mount(void) {
    if (!*host_directory) {
        snprintf(host_directory, sizeof(host_directory), ".pio/host-tests/ui-runs-%lu-%u", (unsigned long)time(NULL), (unsigned)_getpid());
        return _mkdir(host_directory) == 0;
    }
    return true;
}
