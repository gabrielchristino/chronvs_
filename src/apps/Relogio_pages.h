#pragma once
#include "lvgl.h"
void chronvs_Relogio_pages_init(lv_obj_t *parent);
void chronvs_Relogio_pages_show(unsigned page);
void chronvs_Relogio_pages_refresh(void);
bool chronvs_Relogio_pages_back(void);
bool chronvs_Relogio_pages_editing(void);
bool chronvs_Relogio_pages_can_swipe_back(lv_obj_t *target);
bool chronvs_Relogio_pages_can_swipe_vertical(lv_obj_t *target);
bool chronvs_Relogio_pages_vertical(int direction);
