#include "gps_data.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "core/calendar.h"

static bool digits(const char *s, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

static bool utc_time(const char *time, const char *date, int64_t *utc) {
    if (strlen(time) < 6 || strlen(date) != 6 || !digits(time, 6) || !digits(date, 6)) return false;
    if (time[6] && (time[6] != '.' || !time[7] || !digits(time + 7, strlen(time + 7)))) return false;
    int hour = (time[0]-'0')*10+time[1]-'0', minute = (time[2]-'0')*10+time[3]-'0';
    int second = (time[4]-'0')*10+time[5]-'0';
    int day = (date[0]-'0')*10+date[1]-'0', month = (date[2]-'0')*10+date[3]-'0';
    int year = 2000+(date[4]-'0')*10+date[5]-'0';
    if (hour > 23 || minute > 59 || second > 59 || !chronvs_calendar_valid(year, month, day)) return false;
    *utc = 946684800LL + (int64_t)chronvs_calendar_ordinal(year, month, day)*86400 + hour*3600+minute*60+second;
    return true;
}

static bool number(const char *s, double *out) {
    if (!*s || (*s != '-' && (*s < '0' || *s > '9'))) return false;
    char *end;
    double n = strtod(s, &end);
    if (*end || !isfinite(n)) return false;
    *out = n;
    return true;
}

static bool coordinate(const char *s, const char *hemisphere, bool latitude, double *out) {
    double n;
    if (!number(s, &n) || n < 0 || n > 18000 || strlen(hemisphere) != 1) return false;
    unsigned degrees = (unsigned)(n / 100);
    double minutes = n - degrees * 100;
    unsigned limit = latitude ? 90 : 180;
    if (degrees > limit || minutes >= 60 || (degrees == limit && minutes != 0)) return false;
    char h = *hemisphere;
    if (latitude ? (h != 'N' && h != 'S') : (h != 'E' && h != 'W')) return false;
    *out = (degrees + minutes / 60) * ((h == 'S' || h == 'W') ? -1 : 1);
    return true;
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool natural(const char *s, unsigned limit, unsigned *out) {
    if (!*s || strlen(s) > 3 || !digits(s, strlen(s))) return false;
    unsigned n = (unsigned)strtoul(s, NULL, 10);
    if (n > limit) return false;
    *out = n;
    return true;
}

static void sky_update(chronvs_gps_data_t *d, char **fields, unsigned count, uint64_t now) {
    unsigned total, index, visible, signal = 0;
    if (count < 4 || !natural(fields[1], 32, &total) || !total ||
        !natural(fields[2], total, &index) || !index ||
        !natural(fields[3], 99, &visible)) return;
    unsigned groups = (count - 4) / 4, remainder = (count - 4) % 4;
    if (groups > 4 || remainder > 1 ||
        (remainder && *fields[count-1] && !natural(fields[count-1], 99, &signal))) return;
    unsigned strongest = 0;
    bool have_cno = false;
    for (unsigned i = 0; i < groups; ++i) {
        const char *cno = fields[7 + i*4];
        unsigned value;
        if (*cno && !natural(cno, 99, &value)) return;
        if (*cno) { have_cno = true; if (value > strongest) strongest = value; }
    }
    chronvs_gps_sky_t *slot = NULL;
    for (unsigned i = 0; i < CHRONVS_GPS_GSV_SOURCES; ++i)
        if (!strncmp(d->sky[i].talker, fields[0]+1, 2)) { slot = &d->sky[i]; break; }
    if (!slot && index == 1) {
        for (unsigned i = 0; i < CHRONVS_GPS_GSV_SOURCES; ++i)
            if (!d->sky[i].valid || now-d->sky[i].updated_ms >= 5000) { slot = &d->sky[i]; break; }
    }
    if (!slot) return;
    if (index == 1) {
        *slot = (chronvs_gps_sky_t){.visible = visible, .total = total, .signal = signal,
            .next = 1, .valid = true};
        memcpy(slot->talker, fields[0]+1, 2);
    }
    if (!slot->valid || (now-slot->updated_ms >= 5000 && index != 1) ||
        slot->next != index || slot->total != total || slot->visible != visible || slot->signal != signal) return;
    if (have_cno) {
        if (!slot->cno_valid || strongest > slot->cno) slot->cno = strongest;
        slot->cno_valid = true;
    }
    slot->updated_ms = now; slot->next = index+1;
}

bool chronvs_gps_parse(chronvs_gps_data_t *d, const char *line, uint64_t now) {
    size_t len = strlen(line);
    const char *star = strchr(line, '*');
    if (len < 10 || len >= 160 || line[0] != '$' || !star ||
        strlen(star) != 3 || hex(star[1]) < 0 || hex(star[2]) < 0) goto bad;
    unsigned checksum = 0;
    for (const char *p = line + 1; p < star; ++p) checksum ^= (unsigned char)*p;
    if (checksum != (unsigned)(hex(star[1]) * 16 + hex(star[2]))) goto bad;
    if (line[6] != ',' || line[1] < 'A' || line[1] > 'Z' || line[2] < 'A' || line[2] > 'Z') goto bad;
    char copy[160];
    memcpy(copy, line, (size_t)(star - line));
    copy[star - line] = 0;
    char *fields[24] = {copy};
    unsigned count = 1;
    for (char *p = copy; *p && count < 24; ++p)
        if (*p == ',') { *p = 0; fields[count++] = p + 1; }
    ++d->sentences;
    d->sentence_ms = now;
    if (!strcmp(copy + 3, "GSV")) { sky_update(d, fields, count, now); return true; }
    bool gga = !strcmp(copy + 3, "GGA"), rmc = !strcmp(copy + 3, "RMC");
    if (!gga && !rmc) return true;
    /* Never let an invalid/new incomplete fix retain old valid coordinates. */
    d->fix = false;
    if (rmc) d->utc_valid = false;
    if ((gga && count < 11) || (rmc && count < 10)) return true;
    if (gga) {
        double n;
        d->gga_ms = now;
        d->satellites_valid = number(fields[7], &n) && n >= 0 && n <= 99 && floor(n) == n;
        d->satellites = d->satellites_valid ? (unsigned)n : 0;
        d->hdop_valid = number(fields[8], &d->hdop) && d->hdop >= 0;
        d->altitude_valid = false;
        if (strlen(fields[6]) != 1 || fields[6][0] < '1' || fields[6][0] > '5') return true;
    } else {
        d->speed_valid = false;
        if (strcmp(fields[2], "A")) return true;
    }
    double lat, lon;
    unsigned index = gga ? 2 : 3;
    if (!coordinate(fields[index], fields[index + 1], true, &lat) ||
        !coordinate(fields[index + 2], fields[index + 3], false, &lon)) return true;
    d->latitude = lat; d->longitude = lon; d->fix = true; d->position_ms = now;
    if (gga) {
        d->altitude_valid = !strcmp(fields[10], "M") && number(fields[9], &d->altitude_m);
        d->altitude_ms = now;
    } else {
        double knots;
        d->utc_valid = utc_time(fields[1], fields[9], &d->utc);
        d->utc_ms = now;
        d->speed_valid = number(fields[7], &knots) && knots >= 0 && knots <= 2000;
        if (d->speed_valid) d->speed_kmh = knots * 1.852;
        d->speed_ms = now;
    }
    return true;
bad:
    ++d->errors;
    return false;
}

void chronvs_gps_freshen(chronvs_gps_data_t *d, uint64_t now) {
    for (unsigned i = 0; i < CHRONVS_GPS_GSV_SOURCES; ++i)
        if (now - d->sky[i].updated_ms >= 5000) d->sky[i].valid = d->sky[i].cno_valid = false;
    if (now - d->utc_ms >= 5000) d->utc_valid = false;
    if (now - d->position_ms >= 5000) d->fix = false;
    if (!d->fix || now - d->altitude_ms >= 5000) d->altitude_valid = false;
    if (!d->fix || now - d->speed_ms >= 5000) d->speed_valid = false;
    if (now - d->gga_ms >= 5000) d->hdop_valid = d->satellites_valid = false;
}
