#include "services/weather_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static int64_t civil_epoch(int year, int month, int day, int hour, int minute, int second) {
    static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month[month-1] + (month == 2 && year % 4 == 0) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return -1;
    int days = 10957; /* 1970 -> 2000 */
    for (int y = 2000; y < year; ++y) days += 365 + (y % 4 == 0);
    for (int m = 1; m < month; ++m) days += days_in_month[m-1] + (m == 2 && year % 4 == 0);
    return (int64_t)(days + day - 1) * 86400 + hour * 3600 + minute * 60 + second;
}

int64_t chronvs_weather_local_epoch(const chronvs_time_t *t) {
    return t && t->valid ? civil_epoch(2000 + t->year, t->month, t->day,
                                      t->hour, t->minute, t->second) : -1;
}

const char *chronvs_weather_condition(int code) {
    switch (code) {
    case 0: return "Céu limpo";
    case 1: return "Poucas nuvens";
    case 2: return "Parcialmente nublado";
    case 3: return "Nublado";
    case 45: case 48: return "Neblina";
    case 51: case 53: case 55: return "Garoa";
    case 56: case 57: return "Garoa congelante";
    case 61: case 63: case 65: return "Chuva";
    case 66: case 67: return "Chuva congelante";
    case 71: case 73: case 75: case 77: case 85: case 86: return "Neve";
    case 80: case 81: case 82: return "Pancadas de chuva";
    case 95: return "Trovoadas";
    case 96: case 99: return "Trovoadas e granizo";
    default: return NULL;
    }
}

chronvs_weather_icon_t chronvs_weather_icon(int code) {
    if (code == 0) return WEATHER_SUN;
    if (code == 1 || code == 2) return WEATHER_PARTLY_CLOUDY;
    if (code == 3) return WEATHER_CLOUD;
    if (code == 45 || code == 48) return WEATHER_FOG;
    if (code >= 95) return WEATHER_STORM;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WEATHER_SNOW;
    return WEATHER_RAIN;
}

static bool temperature(float value) { return isfinite(value) && value >= -90 && value <= 70; }

bool chronvs_weather_snapshot_valid(const chronvs_weather_snapshot_t *s) {
    return s && s->valid && temperature(s->temperature_c) &&
        temperature(s->apparent_temperature_c) && temperature(s->minimum_c) &&
        temperature(s->maximum_c) && s->minimum_c <= s->maximum_c &&
        s->humidity_percent <= 100 && chronvs_weather_condition(s->weather_code) &&
        s->updated_epoch >= 946684800LL && s->updated_epoch < 4102444800LL;
}

static const cJSON *field(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}
static bool number(const cJSON *item, double low, double high, double *out) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < low || item->valuedouble > high) return false;
    *out = item->valuedouble;
    return true;
}
static bool unit(const cJSON *object, const char *name, const char *expected) {
    const cJSON *item = field(object, name);
    return cJSON_IsString(item) && !strcmp(item->valuestring, expected);
}
static bool one_day(const cJSON *daily, const char *name, double *value) {
    const cJSON *array = field(daily, name);
    return cJSON_IsArray(array) && cJSON_GetArraySize(array) == 1 &&
        number(cJSON_GetArrayItem(array, 0), -90, 70, value);
}
static int decimal(const char *text, unsigned length) {
    int result = 0;
    for (unsigned i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        result = result * 10 + text[i] - '0';
    }
    return result;
}

bool chronvs_weather_parse(const char *json, size_t length, chronvs_weather_snapshot_t *out) {
    if (!json || !out || !length || length > CHRONVS_WEATHER_RESPONSE_LIMIT) return false;
    /* Bound parser recursion on the worker stack, including unknown fields. */
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = json[i];
        if (!c) return false;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    if (quoted || depth) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return false;
    while (end < json + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    bool ok = end == json + length && cJSON_IsObject(root);
    const cJSON *current = field(root, "current"), *daily = field(root, "daily");
    const cJSON *time = field(current, "time"), *dates = field(daily, "time");
    const cJSON *date = cJSON_GetArrayItem(dates, 0);
    const cJSON *cu = field(root, "current_units"), *du = field(root, "daily_units");
    double t, feels, humidity, code, low, high, offset;
    ok = ok && unit(root, "timezone", "America/Sao_Paulo") &&
        number(field(root, "utc_offset_seconds"), -10800, -10800, &offset) &&
        unit(cu, "temperature_2m", "°C") && unit(cu, "apparent_temperature", "°C") &&
        unit(cu, "relative_humidity_2m", "%") && unit(cu, "weather_code", "wmo code") &&
        unit(du, "temperature_2m_min", "°C") && unit(du, "temperature_2m_max", "°C") &&
        number(field(current, "temperature_2m"), -90, 70, &t) &&
        number(field(current, "apparent_temperature"), -90, 70, &feels) &&
        number(field(current, "relative_humidity_2m"), 0, 100, &humidity) && floor(humidity) == humidity &&
        number(field(current, "weather_code"), 0, 99, &code) && floor(code) == code &&
        one_day(daily, "temperature_2m_min", &low) && one_day(daily, "temperature_2m_max", &high) &&
        cJSON_IsString(time) && strlen(time->valuestring) == 16 &&
        cJSON_IsArray(dates) && cJSON_GetArraySize(dates) == 1 && cJSON_IsString(date) &&
        strlen(date->valuestring) == 10 && !strncmp(date->valuestring, time->valuestring, 10);
    if (ok) {
        const char *s = time->valuestring;
        ok = s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':';
        chronvs_weather_snapshot_t candidate = {
            .temperature_c = t, .apparent_temperature_c = feels,
            .minimum_c = low, .maximum_c = high, .humidity_percent = humidity,
            .weather_code = code, .valid = true,
            .updated_epoch = civil_epoch(decimal(s,4), decimal(s+5,2), decimal(s+8,2),
                                          decimal(s+11,2), decimal(s+14,2), 0),
        };
        ok = ok && chronvs_weather_snapshot_valid(&candidate);
        if (ok) *out = candidate;
    }
    cJSON_Delete(root);
    return ok;
}

void chronvs_weather_age(const chronvs_weather_snapshot_t *s, int64_t now, char *text, size_t size) {
    if (!s || !s->valid) { snprintf(text, size, "Sem dados"); return; }
    if (now < s->updated_epoch) {
        snprintf(text, size, "Horário indisponível");
        return;
    }
    int64_t minutes = (now - s->updated_epoch) / 60;
    if (minutes < 1) snprintf(text, size, "Atualizado agora");
    else if (minutes < 60) snprintf(text, size, "Atualizado há %lld min", (long long)minutes);
    else if (minutes < 1440) snprintf(text, size, "Atualizado há %lld h", (long long)(minutes / 60));
    else snprintf(text, size, "Atualizado há %lld d", (long long)(minutes / 1440));
}
