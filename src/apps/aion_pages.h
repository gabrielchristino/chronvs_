#pragma once
#include "lvgl.h"
void chronvs_aion_pages_init(lv_obj_t *parent);
void chronvs_aion_pages_show(unsigned page);
void chronvs_aion_pages_refresh(void);
bool chronvs_aion_pages_back(void);
bool chronvs_aion_pages_editing(void);
bool chronvs_aion_pages_can_swipe_back(lv_obj_t *target);
