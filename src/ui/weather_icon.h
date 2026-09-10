#pragma once

#include "lvgl.h"
#include "services/weather_data.h"

/* Fits the shared vector symbol inside bounds, up to its native 44 px size. */
void chronvs_ui_draw_weather_icon(lv_draw_ctx_t *ctx, const lv_area_t *bounds,
                                  chronvs_weather_icon_t icon);
