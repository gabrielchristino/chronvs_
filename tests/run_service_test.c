#include <assert.h>
#include <direct.h>
#include <process.h>
#include <stdio.h>
#include <time.h>
#include <io.h>
#include <stdbool.h>
static bool fail_sync;
static int host_sync(int fd) { return fail_sync ? -1 : _commit(fd); }
static struct tm *host_gmtime_r(const time_t *value, struct tm *out) {
    return gmtime_s(out, value) == 0 ? out : NULL;
}
#define gmtime_r host_gmtime_r
static char test_directory[64];
#define CHRONVS_RUN_DIRECTORY test_directory
#define fsync host_sync
#include "services/run_service.c"

static uint64_t test_ms = 1000;
static bool requested, sd_ok = true;
static chronvs_gps_snapshot_t gps;
int64_t esp_timer_get_time(void) { return test_ms*1000; }
bool chronvs_sd_mount(void) { return sd_ok; }
void chronvs_gps_recording_request(bool enabled) { requested = enabled; }
static bool workout;
void chronvs_gps_workout_active(bool enabled) { workout=enabled; }
bool chronvs_gps_assist_request(bool force) { (void)force; assert(!workout); return true; }
void chronvs_gps_snapshot(chronvs_gps_snapshot_t *out) { *out = gps; }
static void point(unsigned second, double lat, double lon, double speed) {
    test_ms = 1000 + second*1000;
    gps = (chronvs_gps_snapshot_t){.running = true, .serial_fresh = true, .data = {.fix = true, .utc_valid = true,
        .utc = 1794054920LL+second, .utc_ms = test_ms, .position_ms = test_ms,
        .latitude = lat, .longitude = lon, .speed_valid = true, .speed_kmh = speed,
        .hdop_valid = true, .hdop = 1, .altitude_valid = true, .altitude_m = 750}};
    chronvs_run_poll();
}
int main(void) {
    snprintf(test_directory, sizeof(test_directory), ".pio/host-tests/runs-%lu-%u", (unsigned long)time(NULL), (unsigned)_getpid());
    assert(_mkdir(test_directory) == 0);
    chronvs_run_prepare(); assert(requested && state.state == RUN_READY);
    assert(!chronvs_run_start()); /* No valid UTC/fix. */
    chronvs_run_poll(); assert(!strcmp(state.message, "Preparando GPS"));
    gps.running = true; chronvs_run_poll(); assert(!strcmp(state.message, "Sem NMEA recente"));
    gps.serial_fresh = true; chronvs_run_poll(); assert(!strcmp(state.message, "Buscando satélites"));
    gps.data.fix = true; chronvs_run_poll(); assert(!strcmp(state.message, "Posição OK; aguardando hora"));
    point(0, -23.55, -46.63, 10);
    assert(chronvs_run_start() && state.points == 1);
    chronvs_run_release_preview(); assert(requested); /* Background owner survives navigation. */
    chronvs_run_poll(); assert(state.points == 1); /* Cached data isn't a second point. */
    for (unsigned i=1; i<=10; ++i) point(i, -23.55+i*0.000025, -46.63, 10);
    assert(state.distance_m > 27 && state.distance_m < 29 && state.points == 11);
    assert(chronvs_run_pause());
    point(20, -23.54, -46.63, 10);
    double before = state.distance_m;
    assert(state.active_s == 10 && state.elapsed_s == 20 && state.points == 11);
    assert(chronvs_run_resume());
    point(21, -23.54, -46.63, 10); assert(state.distance_m == before);
    point(22, -23.539975, -46.63, 10); assert(state.distance_m > before);
    before = state.distance_m;
    point(23, 0, 0, 10); assert(state.distance_m == before); /* Impossible GPS jump. */
    gps.data.fix = false; chronvs_run_poll();
    point(30, -23.53995, -46.63, 10); assert(state.distance_m == before); /* Gap has no bridge. */
    for (unsigned i=31; i<40; ++i) point(i, -23.53995+(i%2)*0.000003, -46.63, 0);
    assert(state.distance_m == before); /* Stationary jitter. */
    char original[128]; strcpy(original, csv_path);
    assert(chronvs_run_finish() && !requested && state.state == RUN_SAVED);
    assert(access(final_path, F_OK) == 0 && access(partial_path, F_OK) != 0);
    FILE *file = fopen(final_path, "r"); assert(file);
    char buffer[8192]; size_t n = fread(buffer, 1, sizeof(buffer)-1, file); buffer[n]=0; fclose(file);
    assert(strstr(buffer, "2026-11-07T12:35:20Z") && strstr(buffer, "</trk></gpx>"));
    chronvs_run_prepare(); assert(chronvs_run_start());
    assert(strcmp(original, csv_path)); assert(chronvs_run_finish());
    char names[5][48]; assert(chronvs_run_files(names, 5) == 2);
    chronvs_run_prepare(); sd_ok = false;
    assert(!chronvs_run_start() && state.state == RUN_ERROR && !requested);
    sd_ok = true; chronvs_run_prepare(); assert(chronvs_run_start());
    fail_sync = true; test_ms += 5000; chronvs_run_poll();
    assert(state.state == RUN_ERROR && !requested && csv == NULL && gpx == NULL);
    assert(access(csv_path, F_OK) == 0 && access(partial_path, F_OK) == 0);
    fail_sync = false;
    printf("Run recording: timestamps, distance, pause, background, gaps, jitter, files and SD failure passed\nArtifacts: %s\n", test_directory);
}
