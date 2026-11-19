#pragma once
#include <stdint.h>
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
#define portYIELD_FROM_ISR() ((void)0)
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((ms) / 10)
