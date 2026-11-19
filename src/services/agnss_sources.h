#pragma once
#include "services/agnss_data.h"
/* Worker APIs. Caller mounts SD on UI task before launch; never formats it. */
bool chronvs_agnss_sources_cached(bool sd, bool igs_only, int64_t utc, uint8_t **out, size_t *length, const char **source);
const char *chronvs_agnss_sources_fetch(bool sd, bool igs_only, uint8_t **out, size_t *length, const char **source, void (*memory)(const char *));
