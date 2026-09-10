#pragma once
#include <stddef.h>

/* One persistent TLSF arena for LVGL objects; separate from display buffers. */
void *chronvs_lvgl_pool_alloc(size_t size);
