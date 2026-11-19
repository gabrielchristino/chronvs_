#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define GPIO_NUM_18 18
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_POSEDGE 1
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
int gpio_config(const gpio_config_t *config);
int gpio_install_isr_service(int flags);
int gpio_isr_handler_add(int pin, void (*handler)(void *), void *argument);
int gpio_intr_disable(int pin);
int gpio_intr_enable(int pin);
