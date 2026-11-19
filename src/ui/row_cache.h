#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded RGB565 RLE data, assembled from existing partial draw buffers.
 * Caller owns aligned storage; no allocation or full-size pixel buffer here. */
typedef struct {
    uint16_t *offsets;
    uint32_t *runs;
    uint16_t width, height, next_row;
    uint32_t count, capacity;
    bool valid, blocked;
} chronvs_row_cache_t;

bool chronvs_row_cache_init(chronvs_row_cache_t *cache, void *storage, size_t bytes,
                           uint16_t width, uint16_t height);
void chronvs_row_cache_clear(chronvs_row_cache_t *cache);
bool chronvs_row_cache_record(chronvs_row_cache_t *cache, uint16_t first_y,
                             uint16_t rows, const uint16_t *pixels, size_t stride);
bool chronvs_row_cache_decode(const chronvs_row_cache_t *cache, uint16_t y, uint16_t *pixels);
