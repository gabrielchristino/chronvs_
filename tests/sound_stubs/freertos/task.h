#pragma once
typedef void *TaskHandle_t;
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int, unsigned);
void vTaskDelay(unsigned);
