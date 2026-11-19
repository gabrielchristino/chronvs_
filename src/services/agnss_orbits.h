#pragma once
#include "services/agnss_data.h"

#define CHRONVS_AGNSS_PREDICTION_LIMIT (384 * 1024)
#define CHRONVS_AGNSS_GZIP_LIMIT (2 * 1024 * 1024)
/* Worker-owned, allocate in PSRAM. One nearest, healthy record per GPS PRN. */
typedef struct {
    uint8_t frames[32][82];
    double ages[32];
    uint32_t seen;
    int64_t utc;
} chronvs_agnss_selection_t;
void chronvs_agnss_selection_init(chronvs_agnss_selection_t *s, int64_t utc);
bool chronvs_agnss_selection_add(chronvs_agnss_selection_t *s, const uint8_t frame[82]);
size_t chronvs_agnss_selection_finish(const chronvs_agnss_selection_t *s, uint8_t *out);
bool chronvs_agnss_prediction(const uint8_t *bytes, size_t size, int64_t utc, uint8_t *out, size_t *length);

typedef struct {
    chronvs_agnss_selection_t selected;
    char line[256];
    size_t length;
    bool header, version, failed;
    unsigned row, sv;
    double values[31], toc;
} chronvs_agnss_rinex_t;
void chronvs_agnss_rinex_init(chronvs_agnss_rinex_t *r, int64_t utc);
bool chronvs_agnss_rinex_feed(chronvs_agnss_rinex_t *r, const uint8_t *bytes, size_t size);
size_t chronvs_agnss_rinex_finish(chronvs_agnss_rinex_t *r, uint8_t *out);
