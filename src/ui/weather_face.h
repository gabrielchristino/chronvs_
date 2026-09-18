#pragma once

#include "lvgl.h"

typedef struct {
    const lv_img_dsc_t *image;
    int16_t x;
    int16_t y;
} chronvs_weather_face_layer_t;

extern const chronvs_weather_face_layer_t chronvs_weather_face_layers[5];
