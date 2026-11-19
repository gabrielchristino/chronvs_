#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHRONVS_AGNSS_BODY_LIMIT 8192
#define CHRONVS_AGNSS_BINARY_LIMIT 6144
#define CHRONVS_CASIC_FRAME_LIMIT 512
typedef struct { size_t start, frames; unsigned satellites; bool ionosphere; } chronvs_agnss_blob_t;
typedef struct { double latitude, longitude; float radius_m; bool valid; } chronvs_agnss_reference_t;

uint32_t chronvs_casic_checksum(const uint8_t *frame, size_t payload_size);
size_t chronvs_casic_frame(uint8_t *out, uint8_t cls, uint8_t id, const uint8_t *payload, size_t size);
bool chronvs_casic_valid(const uint8_t *frame, size_t size);
/* UTC -> GPS, with the IERS Bulletin C72 offset valid through 2027-06-30.
 * Refuse future epochs until the leap-second table has been reviewed. */
bool chronvs_agnss_time(int64_t utc, unsigned *week, double *tow);
bool chronvs_agnss_validate(const uint8_t *bytes, size_t size, int64_t utc, chronvs_agnss_blob_t *out);
bool chronvs_agnss_frame_current(const uint8_t *frame, int64_t utc);
size_t chronvs_agnss_aid(uint8_t out[66], int64_t utc, const chronvs_agnss_reference_t *reference);
