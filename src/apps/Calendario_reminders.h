#pragma once
#include "lvgl.h"

void chronvs_Calendario_reminders_open(lv_obj_t *parent, int year, int month, int day);
bool chronvs_Calendario_reminders_active(void);
void chronvs_Calendario_reminders_poll(void);
