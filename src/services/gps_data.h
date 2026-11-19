#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHRONVS_GPS_GSV_SOURCES 4
typedef struct {
    char talker[3];
    unsigned visible, cno, total, next, signal;
    uint64_t updated_ms;
    bool valid, cno_valid;
} chronvs_gps_sky_t;

typedef struct {
    double latitude, longitude, altitude_m, speed_kmh, hdop;
    unsigned satellites;
    bool fix, altitude_valid, speed_valid, hdop_valid, satellites_valid;
    uint64_t sentence_ms, position_ms, altitude_ms, speed_ms, gga_ms;
    uint32_t sentences, errors;
    /* UTC Unix seconds from a valid, positioned RMC (2000..2099). */
    int64_t utc;
    uint64_t utc_ms;
    bool utc_valid;
    /* Per-talker GSV diagnostics. Never sum overlapping constellations/signals. */
    chronvs_gps_sky_t sky[CHRONVS_GPS_GSV_SOURCES];
} chronvs_gps_data_t;

/* Complete NMEA line, including checksum; accepts any two-letter talker. */
bool chronvs_gps_parse(chronvs_gps_data_t *data, const char *line, uint64_t now_ms);
void chronvs_gps_freshen(chronvs_gps_data_t *data, uint64_t now_ms);
