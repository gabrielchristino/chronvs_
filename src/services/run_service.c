#include "services/run_service.h"
#include "services/gps_service.h"
#include "services/sd_service.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "esp_timer.h"

static chronvs_run_snapshot_t state = {.message = "Pronto para correr"};
static FILE *csv, *gpx;
static char csv_path[128], partial_path[128], final_path[128];
static uint64_t started, active_started, active_ms, next_sync;
static int64_t last_utc;
static double last_lat, last_lon;
static bool have_anchor, segment_open;
static unsigned segment;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static bool active(void) { return state.state == RUN_RECORDING || state.state == RUN_PAUSED; }
static bool sync_file(FILE *file) { return !file || (fflush(file) == 0 && fsync(fileno(file)) == 0); }
static bool close_files(void) {
    bool ok = sync_file(csv), other = sync_file(gpx);
    if (csv && fclose(csv)) ok = false;
    if (gpx && fclose(gpx)) other = false;
    csv = gpx = NULL;
    return ok && other;
}
static bool fail(const char *message) {
    close_files();
    state.state = RUN_ERROR; state.message = message;
    chronvs_gps_workout_active(false);
    chronvs_gps_recording_request(false);
    return false;
}
static void times(void) {
    if (!active()) return;
    uint64_t now = now_ms();
    state.elapsed_s = (uint32_t)((now - started) / 1000);
    state.active_s = (uint32_t)((active_ms + (state.state == RUN_RECORDING ? now-active_started : 0)) / 1000);
}
static bool iso_time(int64_t utc, char *text, size_t size) {
    time_t value = (time_t)utc;
    struct tm tm;
    return gmtime_r(&value, &tm) && strftime(text, size, "%Y-%m-%dT%H:%M:%SZ", &tm);
}
static FILE *exclusive(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "w");
    if (!file) close(fd);
    return file;
}

void chronvs_run_prepare(void) {
    if (active()) return;
    state = (chronvs_run_snapshot_t){0};
    chronvs_gps_recording_request(true);
    chronvs_gps_assist_request(false);
    state.state = RUN_READY; state.message = "Aguardando GPS e hora UTC";
}
void chronvs_run_release_preview(void) {
    if (!active()) {
        chronvs_gps_recording_request(false);
        if (state.state == RUN_READY) state.state = RUN_IDLE;
    }
}
bool chronvs_run_start(void) {
    if (active()) return false;
    chronvs_gps_snapshot_t gps;
    chronvs_gps_snapshot(&gps);
    if (!gps.running || !gps.data.fix || !gps.data.utc_valid ||
        now_ms() - gps.data.utc_ms >= 2000) {
        state.message = "Aguarde posição e hora UTC"; return false;
    }
    if (!chronvs_sd_mount()) return fail("SD indisponível (FAT32)");
    char stamp[32];
    time_t utc = (time_t)gps.data.utc;
    struct tm tm;
    if (!gmtime_r(&utc, &tm) || !strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &tm)) return fail("Hora GPS inválida");
    bool opened = false;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        snprintf(state.file, sizeof(state.file), "%s-%02u", stamp, attempt);
        snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", CHRONVS_RUN_DIRECTORY, state.file);
        snprintf(partial_path, sizeof(partial_path), "%s/%s.gpx.part", CHRONVS_RUN_DIRECTORY, state.file);
        snprintf(final_path, sizeof(final_path), "%s/%s.gpx", CHRONVS_RUN_DIRECTORY, state.file);
        /* Never overwrite recordings, including interrupted or partially created pairs. */
        if (!access(csv_path, F_OK) || !access(partial_path, F_OK) || !access(final_path, F_OK)) continue;
        csv = exclusive(csv_path);
        if (!csv) return fail("Falha ao criar arquivo no SD");
        gpx = exclusive(partial_path);
        if (!gpx) return fail("Falha ao criar GPX no SD");
        opened = true; break;
    }
    if (!opened) return fail("Nomes de arquivo ocupados");
    if (fprintf(csv, "utc,elapsed_s,active_s,segment,latitude,longitude,speed_kmh,altitude_m,distance_m\n") < 0 ||
        fprintf(gpx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<gpx version=\"1.1\" creator=\"Chronvs\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
            "<trk><name>Corrida %s</name><type>running</type>\n", state.file) < 0 ||
        !sync_file(csv) || !sync_file(gpx)) return fail("Falha ao gravar no SD");
    state.state = RUN_RECORDING; state.message = "Gravando no SD";
    chronvs_gps_workout_active(true);
    state.points = state.elapsed_s = state.active_s = 0; state.distance_m = 0;
    started = active_started = now_ms(); active_ms = 0; next_sync = started + 5000;
    have_anchor = segment_open = false; segment = 0; last_utc = 0;
    chronvs_gps_recording_request(true);
    chronvs_run_poll();
    return state.state == RUN_RECORDING;
}
bool chronvs_run_pause(void) {
    if (state.state != RUN_RECORDING) return false;
    times(); active_ms += now_ms() - active_started;
    state.state = RUN_PAUSED; state.message = "Pausada";
    have_anchor = false;
    if (segment_open && fprintf(gpx, "</trkseg>\n") < 0) return fail("Falha ao pausar no SD");
    segment_open = false;
    if (!sync_file(csv) || !sync_file(gpx)) return fail("Falha ao salvar pausa");
    return true;
}
bool chronvs_run_resume(void) {
    if (state.state != RUN_PAUSED) return false;
    state.state = RUN_RECORDING; state.message = "Gravando no SD";
    active_started = now_ms(); have_anchor = false;
    /* Do not append a cached pre-pause position. */
    chronvs_gps_snapshot_t gps; chronvs_gps_snapshot(&gps); last_utc = gps.data.utc;
    return true;
}
bool chronvs_run_finish(void) {
    if (!active()) return false;
    times();
    if (fprintf(csv, "#finish,%lu,%lu,%.3f,%lu\n", (unsigned long)state.elapsed_s,
        (unsigned long)state.active_s, state.distance_m, (unsigned long)state.points) < 0 ||
        fprintf(gpx, "%s</trk></gpx>\n", segment_open ? "</trkseg>\n" : "") < 0)
        return fail("Falha ao finalizar no SD");
    if (!close_files()) return fail("Falha ao sincronizar SD");
    if (rename(partial_path, final_path)) return fail("GPX parcial; recuperar no PC");
    state.state = RUN_SAVED; state.message = "Salva: GPX + CSV no SD";
    chronvs_gps_workout_active(false);
    state.speed_valid = false;
    chronvs_gps_recording_request(false);
    return true;
}

static double distance(double lat, double lon) {
    const double rad = 0.017453292519943295;
    double a = sin((lat-last_lat)*rad/2), b = sin((lon-last_lon)*rad/2);
    double h = a*a + cos(last_lat*rad)*cos(lat*rad)*b*b;
    return 12742000.0 * asin(sqrt(fmin(1.0, h)));
}
void chronvs_run_poll(void) {
    if (!active() && state.state != RUN_READY) return;
    chronvs_gps_snapshot_t gps; chronvs_gps_snapshot(&gps);
    const chronvs_gps_data_t *d = &gps.data;
    uint64_t now = now_ms();
    state.fix = gps.running && d->fix && d->utc_valid && now-d->utc_ms < 2000;
    state.speed_valid = state.state == RUN_RECORDING && state.fix && d->speed_valid;
    state.speed_kmh = d->speed_kmh;
    if (state.state == RUN_READY) {
        state.message = gps.error ? "Falha na UART" : !gps.running ? "Preparando GPS" :
            !gps.serial_fresh ? "Sem NMEA recente" : !d->fix ? "Buscando satélites" :
            !state.fix ? "Posição OK; aguardando hora" : "GPS pronto para iniciar";
        return;
    }
    times();
    if (state.state == RUN_RECORDING) {
        if (!state.fix || (d->hdop_valid && d->hdop > 5)) {
            state.speed_valid = false;
            have_anchor = false;
            if (segment_open) {
                if (fprintf(gpx, "</trkseg>\n") < 0) { fail("Falha no SD"); return; }
                segment_open = false;
            }
            state.message = "Sem posição confiável";
        } else if (d->utc > last_utc) {
            double delta = have_anchor ? distance(d->latitude, d->longitude) : 0;
            int64_t dt = d->utc - last_utc;
            /* Break segments after gaps or impossible running jumps (>36 km/h).
             * Require displacement >=2 m and reported motion to reduce stationary drift. */
            if (have_anchor && (dt > 5 || delta > 10.0*dt)) {
                have_anchor = false;
                if (segment_open && fprintf(gpx, "</trkseg>\n") < 0) { fail("Falha no SD"); return; }
                segment_open = false; delta = 0;
            }
            if (have_anchor && delta >= 2 && (!d->speed_valid || d->speed_kmh >= 1.5)) state.distance_m += delta;
            bool move_anchor = !have_anchor || delta >= 2;
            if (!segment_open) {
                if (fprintf(gpx, "<trkseg>\n") < 0) { fail("Falha no SD"); return; }
                segment_open = true; ++segment;
            }
            char timestamp[32], speed[32] = "", altitude[32] = "";
            if (!iso_time(d->utc, timestamp, sizeof(timestamp))) { fail("Hora GPS inválida"); return; }
            if (d->speed_valid) snprintf(speed, sizeof(speed), "%.3f", d->speed_kmh);
            if (d->altitude_valid) snprintf(altitude, sizeof(altitude), "%.2f", d->altitude_m);
            if (fprintf(csv, "%lld,%lu,%lu,%u,%.7f,%.7f,%s,%s,%.3f\n", (long long)d->utc,
                (unsigned long)state.elapsed_s, (unsigned long)state.active_s, segment,
                d->latitude, d->longitude, speed, altitude, state.distance_m) < 0 ||
                fprintf(gpx, "<trkpt lat=\"%.7f\" lon=\"%.7f\">", d->latitude, d->longitude) < 0 ||
                (d->altitude_valid && fprintf(gpx, "<ele>%s</ele>", altitude) < 0) ||
                fprintf(gpx, "<time>%s</time></trkpt>\n", timestamp) < 0) { fail("Falha ao gravar no SD"); return; }
            last_utc = d->utc;
            if (move_anchor) { last_lat = d->latitude; last_lon = d->longitude; }
            have_anchor = true; ++state.points; state.message = "Gravando no SD";
        }
    }
    if (now >= next_sync) {
        if (!sync_file(csv) || !sync_file(gpx)) { fail("Falha ao sincronizar SD"); return; }
        next_sync = now + 5000;
    }
}
void chronvs_run_snapshot(chronvs_run_snapshot_t *out) { times(); *out = state; }

unsigned chronvs_run_files(char names[][48], unsigned capacity) {
    if (!capacity || !chronvs_sd_mount()) return 0;
    DIR *dir = opendir(CHRONVS_RUN_DIRECTORY);
    if (!dir) return 0;
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t len = strlen(entry->d_name);
        if (len >= 48 || len < 5 || strcmp(entry->d_name + len-4, ".csv")) continue;
        unsigned at = 0;
        while (at < count && strcmp(names[at], entry->d_name) > 0) ++at;
        if (at >= capacity) continue;
        if (count < capacity) ++count;
        for (unsigned i = count-1; i > at; --i) strcpy(names[i], names[i-1]);
        strcpy(names[at], entry->d_name);
    }
    closedir(dir);
    return count;
}
