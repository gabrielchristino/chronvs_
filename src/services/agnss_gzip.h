#pragma once
#include "services/agnss_orbits.h"
/* Worker only. Bounded gzip, CRC32 + ISIZE checked before any result is used. */
bool chronvs_agnss_gzip_rinex(const uint8_t *bytes, size_t size, int64_t utc, uint8_t *out, size_t *length);
