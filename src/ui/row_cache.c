#include "ui/row_cache.h"

void chronvs_row_cache_clear(chronvs_row_cache_t *cache) {
    cache->next_row = 0;
    cache->count = 0;
    cache->valid = cache->blocked = false;
}

bool chronvs_row_cache_init(chronvs_row_cache_t *cache, void *storage, size_t bytes,
                           uint16_t width, uint16_t height) {
    *cache = (chronvs_row_cache_t){0};
    const size_t header = (((size_t)height + 1) * sizeof(uint16_t) + 3) & ~(size_t)3;
    if (!storage || ((uintptr_t)storage & 3) || !width || !height || bytes < header + 4) return false;
    size_t capacity = (bytes - header) / sizeof(uint32_t);
    if (capacity > UINT16_MAX) capacity = UINT16_MAX;
    cache->offsets = storage;
    cache->runs = (uint32_t *)((uint8_t *)storage + header);
    cache->width = width; cache->height = height; cache->capacity = (uint32_t)capacity;
    return true;
}

bool chronvs_row_cache_record(chronvs_row_cache_t *cache, uint16_t first_y,
                             uint16_t rows, const uint16_t *pixels, size_t stride) {
    if (!cache->runs || cache->valid || cache->blocked || !pixels || !rows || stride < cache->width ||
        (uint32_t)first_y + rows > cache->height) return false;
    if (first_y == 0) chronvs_row_cache_clear(cache);
    if (first_y != cache->next_row) { chronvs_row_cache_clear(cache); return false; }
    for (unsigned y = first_y; y < (unsigned)first_y + rows; ++y, pixels += stride) {
        cache->offsets[y] = (uint16_t)cache->count;
        for (unsigned x = 0; x < cache->width;) {
            const uint16_t color = pixels[x];
            do { ++x; } while (x < cache->width && pixels[x] == color);
            if (cache->count >= cache->capacity) {
                chronvs_row_cache_clear(cache); cache->blocked = true; return false;
            }
            cache->runs[cache->count++] = ((uint32_t)x << 16) | color;
        }
        cache->next_row = (uint16_t)(y + 1);
    }
    cache->offsets[cache->next_row] = (uint16_t)cache->count;
    cache->valid = cache->next_row == cache->height;
    return true;
}

bool chronvs_row_cache_decode(const chronvs_row_cache_t *cache, uint16_t y, uint16_t *pixels) {
    if (!cache->valid || !pixels || y >= cache->height) return false;
    unsigned x = 0;
    for (unsigned i = cache->offsets[y]; i < cache->offsets[y + 1]; ++i) {
        const unsigned end = cache->runs[i] >> 16;
        if (end <= x || end > cache->width) return false;
        const uint16_t color = (uint16_t)cache->runs[i];
        while (x < end) pixels[x++] = color;
    }
    return x == cache->width;
}
