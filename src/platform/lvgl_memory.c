#include "platform/lvgl_memory.h"

#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

void *chronvs_lvgl_pool_alloc(size_t size) {
    static void *pool;
    static size_t pool_size;
    /* lv_mem_deinit() reinitializes TLSF: reuse the arena instead of leaking it. */
    if (!pool) {
        pool = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pool) {
            ESP_LOGE("lvgl_memory", "Could not allocate LVGL arena in PSRAM");
            abort();
        }
        pool_size = size;
    }
    if (size != pool_size) abort();
    return pool;
}
