#pragma once
#include "freertos/FreeRTOS.h"
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
