#pragma once
#include "lvgl.h"

void chronvs_hemera_reminders_open(lv_obj_t *parent, int year, int month, int day);
bool chronvs_hemera_reminders_active(void);
void chronvs_hemera_reminders_poll(void);
